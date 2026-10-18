#include "logins.h"

#include "cdp.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUuid>

namespace zappe {
namespace {

LoginStep parseStep(const QJsonObject &obj)
{
    LoginStep step;
    step.op = obj.value(QStringLiteral("op")).toString();
    step.url = obj.value(QStringLiteral("url")).toString();
    step.selector = obj.value(QStringLiteral("selector")).toString();
    step.field = obj.value(QStringLiteral("field")).toString();
    step.prompt = obj.value(QStringLiteral("prompt")).toString();
    step.secret = obj.value(QStringLiteral("secret")).toBool();
    step.optional = obj.value(QStringLiteral("optional")).toBool();
    return step;
}

bool knownField(const QString &field)
{
    return field == QLatin1String("email") || field == QLatin1String("password") || field == QLatin1String("code")
           || field == QLatin1String("profile");
}

} // namespace

LoginCatalog parseLoginCatalog(const QByteArray &json)
{
    LoginCatalog catalog;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        catalog.error = error.errorString();
        return catalog;
    }
    const QJsonArray services = doc.object().value(QStringLiteral("services")).toArray();
    for (const QJsonValue &value : services) {
        const QJsonObject obj = value.toObject();
        LoginService service;
        service.id = obj.value(QStringLiteral("id")).toString();
        service.name = obj.value(QStringLiteral("name")).toString();
        service.homeUrl = obj.value(QStringLiteral("homeUrl")).toString();
        service.loginUrl = obj.value(QStringLiteral("loginUrl")).toString();
        service.readySelector = obj.value(QStringLiteral("readySelector")).toString();
        for (const QJsonValue &stepValue : obj.value(QStringLiteral("steps")).toArray())
            service.steps.append(parseStep(stepValue.toObject()));
        if (!service.id.isEmpty())
            catalog.services.append(service);
    }
    catalog.ok = !catalog.services.isEmpty();
    return catalog;
}

LoginCatalog loadLoginCatalog()
{
    QFile file(QStringLiteral(":/zappe/data/logins.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        LoginCatalog catalog;
        catalog.error = QStringLiteral("logins.json ausente");
        return catalog;
    }
    return parseLoginCatalog(file.readAll());
}

const LoginService *findService(const LoginCatalog &catalog, const QString &id)
{
    for (const LoginService &service : catalog.services) {
        if (service.id.compare(id, Qt::CaseInsensitive) == 0)
            return &service;
    }
    return nullptr;
}

void appendStreamingShelf(CatalogFile &catalog, const LoginCatalog &logins)
{
    for (const ShelfData &shelf : catalog.shelves) {
        if (shelf.id == QLatin1String("apps"))
            return;
    }
    ShelfData apps;
    apps.id = QStringLiteral("apps");
    apps.title = QStringLiteral("Streaming");
    apps.status = QStringLiteral("ok");
    for (const LoginService &service : logins.services) {
        RowData row;
        row.title = service.name;
        row.service = service.id;
        row.serviceLabel = service.name;
        row.href = service.homeUrl;
        row.wide = true;
        apps.rows.append(row);
    }
    int insertAt = 0;
    for (int i = 0; i < catalog.shelves.size(); ++i) {
        if (catalog.shelves.at(i).live)
            insertAt = i + 1;
    }
    catalog.shelves.insert(insertAt, apps);
}

WebhookConfig loadWebhookConfig(const QString &dataDir)
{
    WebhookConfig config;
    config.url = qEnvironmentVariable("ZAPPE_LOGIN_WEBHOOK").trimmed();
    config.token = qEnvironmentVariable("ZAPPE_LOGIN_WEBHOOK_TOKEN").trimmed();
    config.inboxFile = dataDir + QStringLiteral("/login-inbox.json");
    QFile file(dataDir + QStringLiteral("/login-webhook.json"));
    if (!file.open(QIODevice::ReadOnly))
        return config;
    const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
    if (config.url.isEmpty())
        config.url = obj.value(QStringLiteral("url")).toString().trimmed();
    if (config.token.isEmpty())
        config.token = obj.value(QStringLiteral("token")).toString().trimmed();
    const QString inbox = obj.value(QStringLiteral("inbox")).toString().trimmed();
    if (!inbox.isEmpty())
        config.inboxFile = inbox.startsWith(QLatin1Char('/')) ? inbox : dataDir + QLatin1Char('/') + inbox;
    return config;
}

bool webhookEnabled(const WebhookConfig &config)
{
    return !config.url.trimmed().isEmpty();
}

QByteArray webhookBody(const QString &requestId, const QString &service, const QString &field, const QString &prompt,
                       bool secret)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("requestId"), requestId);
    obj.insert(QStringLiteral("service"), service);
    obj.insert(QStringLiteral("field"), field);
    obj.insert(QStringLiteral("prompt"), prompt);
    obj.insert(QStringLiteral("secret"), secret);
    obj.insert(QStringLiteral("source"), QStringLiteral("zappe"));
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QString inboxValue(const QByteArray &json, const QString &requestId)
{
    const QJsonObject obj = QJsonDocument::fromJson(json).object();
    if (obj.isEmpty())
        return {};
    const QString id = obj.value(QStringLiteral("requestId")).toString();
    if (!id.isEmpty() && id != requestId)
        return {};
    return obj.value(QStringLiteral("value")).toString();
}

LoginRunner::LoginRunner(QObject *parent)
    : QObject(parent)
    , m_cdp(new CdpClient(this))
    , m_http(new QNetworkAccessManager(this))
    , m_inbox(new QTimer(this))
{
    m_inbox->setInterval(400);
    connect(m_cdp, &CdpClient::attached, this, &LoginRunner::afterReady);
    connect(m_cdp, &CdpClient::failed, this, [this](const QString &message) {
        if (!m_active)
            return;
        m_active = false;
        emit failed(message);
    });
    connect(m_cdp, &CdpClient::navigated, this, [this] {
        if (!m_active || m_waiting || m_step < 0)
            return;
        QTimer::singleShot(700, this, [this] {
            if (m_active && !m_waiting && m_step >= 0)
                runStep();
        });
    });
    connect(m_cdp, &CdpClient::historyResult, this, &LoginRunner::historyResult);
    connect(m_cdp, &CdpClient::evaluated, this, [this](const QString &value, bool ok) {
        if (!m_active || m_waiting)
            return;
        if (!ok) {
            emit failed(value);
            m_active = false;
            return;
        }
        if (m_step < 0) {
            if (value == QLatin1String("ok"))
                finishHome();
            else
                runStep();
            return;
        }
        if (m_step >= m_service.steps.size())
            return;
        const LoginStep step = m_service.steps.at(m_step);
        if (step.op == QLatin1String("ask")) {
            if (value == QLatin1String("missing") && step.optional) {
                ++m_step;
                runStep();
                return;
            }
            ask(step);
            return;
        }
        if (value == QLatin1String("missing")) {
            if (step.optional || m_waits < 4) {
                ++m_waits;
                QTimer::singleShot(400, this, [this] {
                    if (m_active)
                        runStep();
                });
                if (m_waits >= 4 && step.optional) {
                    ++m_step;
                    m_waits = 0;
                }
                return;
            }
            emit status(QStringLiteral("Seletor não encontrado, seguindo."));
        }
        m_waits = 0;
        ++m_step;
        runStep();
    });
    connect(m_inbox, &QTimer::timeout, this, &LoginRunner::pollInbox);
}

void LoginRunner::start(const LoginService &service, int port, const QString &dataDir)
{
    m_service = service;
    m_port = port;
    m_dataDir = dataDir;
    m_webhook = loadWebhookConfig(dataDir);
    m_step = 0;
    m_waits = 0;
    m_waiting = false;
    m_active = true;
    emit status(QStringLiteral("Abrindo ") + service.name);
    attach();
}

void LoginRunner::attach()
{
    QTimer::singleShot(400, this, [this] {
        if (m_active)
            m_cdp->openPage(m_port, QStringLiteral("about:blank"));
    });
}

void LoginRunner::afterReady()
{
    if (!m_active)
        return;
    m_step = -1;
    m_cdp->navigate(m_service.homeUrl);
    QTimer::singleShot(1200, this, [this] {
        if (!m_active || m_step != -1)
            return;
        if (m_service.readySelector.isEmpty())
            runStep();
        else
            m_cdp->evaluate(existsScript(m_service.readySelector));
    });
}

void LoginRunner::runStep()
{
    if (!m_active || m_waiting)
        return;
    if (m_step < 0)
        m_step = 0;
    if (m_step >= m_service.steps.size()) {
        finishHome();
        return;
    }
    const LoginStep step = m_service.steps.at(m_step);
    if (step.op == QLatin1String("navigate")) {
        ++m_step;
        m_cdp->navigate(step.url);
        return;
    }
    if (step.op == QLatin1String("click")) {
        m_cdp->evaluate(clickScript(step.selector));
        return;
    }
    if (step.op == QLatin1String("ask")) {
        if (step.optional && !step.selector.isEmpty())
            m_cdp->evaluate(existsScript(step.selector));
        else
            ask(step);
        return;
    }
    ++m_step;
    runStep();
}

void LoginRunner::ask(const LoginStep &step)
{
    if (!knownField(step.field)) {
        ++m_step;
        runStep();
        return;
    }
    m_waiting = true;
    m_requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    emit promptRequested(m_service.name, step.field, step.prompt, step.secret);
    if (webhookEnabled(m_webhook)) {
        QNetworkRequest request{QUrl(m_webhook.url)};
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        if (!m_webhook.token.isEmpty())
            request.setRawHeader("Authorization", QByteArray("Bearer ") + m_webhook.token.toUtf8());
        QNetworkReply *reply = m_http->post(request, webhookBody(m_requestId, m_service.id, step.field, step.prompt, step.secret));
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            const QByteArray body = reply->readAll();
            reply->deleteLater();
            const QString value = inboxValue(body, m_requestId);
            if (m_waiting && !value.isEmpty())
                submit(value);
        });
    } else {
        emit status(QStringLiteral("Webhook não configurado — use a tela ou login-inbox.json."));
    }
    m_inbox->start();
}

void LoginRunner::pollInbox()
{
    if (!m_waiting || m_webhook.inboxFile.isEmpty())
        return;
    QFile file(m_webhook.inboxFile);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QString value = inboxValue(file.readAll(), m_requestId);
    file.close();
    if (value.isEmpty())
        return;
    file.remove();
    submit(value);
}

void LoginRunner::submit(const QString &value)
{
    if (!m_waiting || !m_active)
        return;
    const QString text = value.trimmed();
    if (text.isEmpty())
        return;
    m_waiting = false;
    m_inbox->stop();
    if (m_step < 0 || m_step >= m_service.steps.size())
        return;
    const LoginStep step = m_service.steps.at(m_step);
    ++m_step;
    if (step.field == QLatin1String("profile") || step.selector.isEmpty())
        m_cdp->evaluate(profileScript(text));
    else
        m_cdp->evaluate(fillScript(step.selector, text));
}

void LoginRunner::cancel()
{
    m_active = false;
    m_waiting = false;
    m_inbox->stop();
}

void LoginRunner::historyBack()
{
    m_cdp->historyBack();
}

void LoginRunner::dispatchKey(const QString &key, const QString &code, int virtualKey)
{
    m_cdp->dispatchKey(key, code, virtualKey);
}

void LoginRunner::dispatchAction(const QString &action)
{
    m_cdp->dispatchAction(action);
}

void LoginRunner::finishHome()
{
    if (!m_active)
        return;
    m_active = false;
    m_waiting = false;
    m_inbox->stop();
    m_cdp->navigate(m_service.homeUrl);
    emit playing(m_service.homeUrl);
}

} // namespace zappe
