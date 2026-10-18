#include "cdp.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QWebSocket>

#include <cstdio>

namespace zappe {

QByteArray cdpCommand(int id, const QString &method, const QJsonObject &params)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("method"), method);
    obj.insert(QStringLiteral("params"), params);
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QString jsString(const QString &value)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}

QString fillScript(const QString &selector, const QString &value)
{
    return QStringLiteral("(() => { const el = document.querySelector(%1); if (!el) return 'missing';"
                          " el.focus(); el.value = %2;"
                          " el.dispatchEvent(new Event('input', {bubbles:true}));"
                          " el.dispatchEvent(new Event('change', {bubbles:true})); return 'ok'; })()")
        .arg(jsString(selector), jsString(value));
}

QString clickScript(const QString &selector)
{
    return QStringLiteral("(() => { const el = document.querySelector(%1); if (!el) return 'missing';"
                          " el.click(); return 'ok'; })()")
        .arg(jsString(selector));
}

QString existsScript(const QString &selector)
{
    return QStringLiteral("(() => document.querySelector(%1) ? 'ok' : 'missing')()").arg(jsString(selector));
}

QString historyBackScript()
{
    return QStringLiteral("(() => { try { if (window.history.length > 1) { window.history.back(); return 'back'; } }"
                          " catch (e) { return 'root'; } return 'root'; })()");
}

QString historyGoBackScript()
{
    return QStringLiteral("(() => { try { window.history.back(); return 'back'; } catch (e) { return 'root'; } })()");
}

bool chromeCanGoBack(int currentIndex)
{
    return currentIndex > 0;
}

QVector<PlayerKeyEvent> playerKeyEvents(const QString &action)
{
    const QString name = action.trimmed().toLower();
    QString key;
    QString code;
    int vk = 0;
    if (name == QLatin1String("left")) {
        key = QStringLiteral("ArrowLeft");
        code = key;
        vk = 37;
    } else if (name == QLatin1String("right")) {
        key = QStringLiteral("ArrowRight");
        code = key;
        vk = 39;
    } else if (name == QLatin1String("up")) {
        key = QStringLiteral("ArrowUp");
        code = key;
        vk = 38;
    } else if (name == QLatin1String("down")) {
        key = QStringLiteral("ArrowDown");
        code = key;
        vk = 40;
    } else if (name == QLatin1String("ok")) {
        key = QStringLiteral("Enter");
        code = key;
        vk = 13;
    } else if (name == QLatin1String("play")) {
        key = QStringLiteral(" ");
        code = QStringLiteral("Space");
        vk = 32;
    } else if (name == QLatin1String("back")) {
        key = QStringLiteral("BrowserBack");
        code = key;
        vk = 166;
    } else {
        return {};
    }
    QVector<PlayerKeyEvent> events;
    for (const char *type : {"rawKeyDown", "keyDown", "keyUp"}) {
        PlayerKeyEvent event;
        event.type = QString::fromLatin1(type);
        event.key = key;
        event.code = code;
        event.virtualKey = vk;
        events.append(event);
    }
    return events;
}

QString profileScript(const QString &name)
{
    return QStringLiteral("(() => { const want = %1.toLowerCase();"
                          " const nodes = [...document.querySelectorAll('a,button,[role=button],li,span')];"
                          " const el = nodes.find(n => (n.innerText || '').toLowerCase().includes(want));"
                          " if (!el) return 'missing'; el.click(); return 'ok'; })()")
        .arg(jsString(name));
}

CdpClient::CdpClient(QObject *parent)
    : QObject(parent)
    , m_http(new QNetworkAccessManager(this))
{
}

void CdpClient::attach(int port)
{
    m_port = port;
    const QUrl url(QStringLiteral("http://127.0.0.1:%1/json/list").arg(port));
    QNetworkReply *reply = m_http->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray body = reply->readAll();
        const QString error = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
        reply->deleteLater();
        if (!error.isEmpty()) {
            emit failed(error);
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        QString ws;
        for (const QJsonValue &value : doc.array()) {
            const QJsonObject obj = value.toObject();
            if (obj.value(QStringLiteral("type")).toString() == QLatin1String("page")) {
                ws = obj.value(QStringLiteral("webSocketDebuggerUrl")).toString();
                if (!ws.isEmpty())
                    break;
            }
        }
        if (ws.isEmpty()) {
            emit failed(QStringLiteral("CDP sem aba page"));
            return;
        }
        openSocket(ws);
    });
}

void CdpClient::openSocket(const QString &url)
{
    if (m_socket) {
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_connected = false;
    m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_socket, &QWebSocket::connected, this, [this] {
        m_connected = true;
        emit attached();
    });
    connect(m_socket, &QWebSocket::textMessageReceived, this, &CdpClient::onMessage);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this,
            [this](QAbstractSocket::SocketError) {
                emit failed(m_socket ? m_socket->errorString() : QStringLiteral("websocket CDP"));
            });
    m_socket->open(QUrl(url));
}

void CdpClient::send(const QString &method, const QJsonObject &params)
{
    if (!m_socket || !m_connected)
        return;
    const int id = m_nextId++;
    if (method == QLatin1String("Page.navigate"))
        m_pendingNav = id;
    if (method == QLatin1String("Runtime.evaluate") && m_pendingHistory != id)
        m_pendingEval = id;
    m_socket->sendTextMessage(QString::fromUtf8(cdpCommand(id, method, params)));
}

void CdpClient::navigate(const QString &url)
{
    QJsonObject params;
    params.insert(QStringLiteral("url"), url);
    send(QStringLiteral("Page.navigate"), params);
}

void CdpClient::evaluate(const QString &expression)
{
    QJsonObject params;
    params.insert(QStringLiteral("expression"), expression);
    params.insert(QStringLiteral("returnByValue"), true);
    send(QStringLiteral("Runtime.evaluate"), params);
}

void CdpClient::sendHistoryEval(const QString &expression)
{
    QJsonObject params;
    params.insert(QStringLiteral("expression"), expression);
    params.insert(QStringLiteral("returnByValue"), true);
    const int id = m_nextId++;
    m_pendingHistory = id;
    m_historyStage = 2;
    m_socket->sendTextMessage(QString::fromUtf8(cdpCommand(id, QStringLiteral("Runtime.evaluate"), params)));
}

void CdpClient::historyBack()
{
    if (!m_socket || !m_connected) {
        emit historyResult(false);
        return;
    }
    const int id = m_nextId++;
    m_pendingHistory = id;
    m_historyStage = 1;
    m_socket->sendTextMessage(
        QString::fromUtf8(cdpCommand(id, QStringLiteral("Page.getNavigationHistory"), QJsonObject())));
}

void CdpClient::dispatchKey(const QString &key, const QString &code, int virtualKey)
{
    if (!m_socket || !m_connected)
        return;
    const QStringList types = {QStringLiteral("rawKeyDown"), QStringLiteral("keyDown"), QStringLiteral("keyUp")};
    for (const QString &type : types) {
        QJsonObject params;
        params.insert(QStringLiteral("type"), type);
        params.insert(QStringLiteral("key"), key);
        params.insert(QStringLiteral("code"), code);
        params.insert(QStringLiteral("windowsVirtualKeyCode"), virtualKey);
        params.insert(QStringLiteral("nativeVirtualKeyCode"), virtualKey);
        if (key == QLatin1String(" ")) {
            params.insert(QStringLiteral("text"), QStringLiteral(" "));
            params.insert(QStringLiteral("unmodifiedText"), QStringLiteral(" "));
        }
        const int id = m_nextId++;
        m_socket->sendTextMessage(
            QString::fromUtf8(cdpCommand(id, QStringLiteral("Input.dispatchKeyEvent"), params)));
    }
}

void CdpClient::dispatchAction(const QString &action)
{
    if (!m_socket || !m_connected)
        return;
    for (const PlayerKeyEvent &event : playerKeyEvents(action)) {
        QJsonObject params;
        params.insert(QStringLiteral("type"), event.type);
        params.insert(QStringLiteral("key"), event.key);
        params.insert(QStringLiteral("code"), event.code);
        params.insert(QStringLiteral("windowsVirtualKeyCode"), event.virtualKey);
        params.insert(QStringLiteral("nativeVirtualKeyCode"), event.virtualKey);
        if (event.key == QLatin1String(" ")) {
            params.insert(QStringLiteral("text"), QStringLiteral(" "));
            params.insert(QStringLiteral("unmodifiedText"), QStringLiteral(" "));
        }
        const int id = m_nextId++;
        m_socket->sendTextMessage(
            QString::fromUtf8(cdpCommand(id, QStringLiteral("Input.dispatchKeyEvent"), params)));
    }
}

void CdpClient::onMessage(const QString &message)
{
    const QJsonObject obj = QJsonDocument::fromJson(message.toUtf8()).object();
    const int id = obj.value(QStringLiteral("id")).toInt();
    if (id == 0)
        return;
    if (obj.contains(QStringLiteral("error"))) {
        const QString text = obj.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
        if (id == m_pendingHistory) {
            if (m_historyStage == 1) {
                sendHistoryEval(historyBackScript());
                return;
            }
            m_pendingHistory = 0;
            m_historyStage = 0;
            emit historyResult(false);
            return;
        }
        if (id == m_pendingEval)
            emit evaluated(text, false);
        else
            emit failed(text);
        return;
    }
    if (id == m_pendingNav) {
        m_pendingNav = 0;
        emit navigated();
        return;
    }
    if (id == m_pendingHistory) {
        const QJsonObject result = obj.value(QStringLiteral("result")).toObject();
        if (m_historyStage == 1) {
            const int index = result.value(QStringLiteral("currentIndex")).toInt(-1);
            if (index < 0) {
                sendHistoryEval(historyBackScript());
                return;
            }
            if (chromeCanGoBack(index)) {
                sendHistoryEval(historyGoBackScript());
                return;
            }
            m_pendingHistory = 0;
            m_historyStage = 0;
            emit historyResult(false);
            return;
        }
        m_pendingHistory = 0;
        m_historyStage = 0;
        const QString value = result.value(QStringLiteral("result")).toObject().value(QStringLiteral("value")).toString();
        emit historyResult(value == QLatin1String("back"));
        return;
    }
    if (id == m_pendingEval) {
        m_pendingEval = 0;
        const QJsonObject result = obj.value(QStringLiteral("result")).toObject().value(QStringLiteral("result")).toObject();
        emit evaluated(result.value(QStringLiteral("value")).toString(), true);
    }
}

} // namespace zappe
