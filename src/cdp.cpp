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
    if (method == QLatin1String("Runtime.evaluate"))
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

void CdpClient::onMessage(const QString &message)
{
    const QJsonObject obj = QJsonDocument::fromJson(message.toUtf8()).object();
    const int id = obj.value(QStringLiteral("id")).toInt();
    if (id == 0)
        return;
    if (obj.contains(QStringLiteral("error"))) {
        const QString text = obj.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
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
    if (id == m_pendingEval) {
        m_pendingEval = 0;
        const QJsonObject result = obj.value(QStringLiteral("result")).toObject().value(QStringLiteral("result")).toObject();
        emit evaluated(result.value(QStringLiteral("value")).toString(), true);
    }
}

} // namespace zappe
