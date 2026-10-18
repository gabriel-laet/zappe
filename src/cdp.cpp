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
    } else if (name == QLatin1String("ok") || name == QLatin1String("play")) {
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

void CdpClient::enableNetwork()
{
    if (!m_socket || !m_connected)
        return;
    m_pendingEnable = m_nextId++;
    m_socket->sendTextMessage(QString::fromUtf8(cdpCommand(m_pendingEnable, QStringLiteral("Network.enable"), QJsonObject())));
}

void CdpClient::getResponseBody(const QString &requestId)
{
    if (!m_socket || !m_connected || requestId.isEmpty())
        return;
    const int id = m_nextId++;
    m_pendingBodies.insert(id, requestId);
    QJsonObject params;
    params.insert(QStringLiteral("requestId"), requestId);
    m_socket->sendTextMessage(QString::fromUtf8(cdpCommand(id, QStringLiteral("Network.getResponseBody"), params)));
}

void CdpClient::closeBrowser()
{
    if (!m_socket || !m_connected)
        return;
    m_pendingClose = m_nextId++;
    m_socket->sendTextMessage(QString::fromUtf8(cdpCommand(m_pendingClose, QStringLiteral("Browser.close"), QJsonObject())));
}

void CdpClient::dispatchMouseClick(int x, int y)
{
    if (!m_socket || !m_connected || x <= 0 || y <= 0)
        return;
    const QStringList types = {QStringLiteral("mousePressed"), QStringLiteral("mouseReleased")};
    for (const QString &type : types) {
        QJsonObject params;
        params.insert(QStringLiteral("type"), type);
        params.insert(QStringLiteral("x"), x);
        params.insert(QStringLiteral("y"), y);
        params.insert(QStringLiteral("button"), QStringLiteral("left"));
        params.insert(QStringLiteral("clickCount"), 1);
        const int id = m_nextId++;
        m_socket->sendTextMessage(QString::fromUtf8(cdpCommand(id, QStringLiteral("Input.dispatchMouseEvent"), params)));
    }
}

void CdpClient::captureScreenshot()
{
    if (!m_socket || !m_connected)
        return;
    m_pendingShot = m_nextId++;
    QJsonObject params;
    params.insert(QStringLiteral("format"), QStringLiteral("jpeg"));
    params.insert(QStringLiteral("quality"), 40);
    m_socket->sendTextMessage(QString::fromUtf8(cdpCommand(m_pendingShot, QStringLiteral("Page.captureScreenshot"), params)));
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
    const QString method = obj.value(QStringLiteral("method")).toString();
    if (!method.isEmpty() && !obj.contains(QStringLiteral("id"))) {
        const QJsonObject params = obj.value(QStringLiteral("params")).toObject();
        if (method == QLatin1String("Network.responseReceived")) {
            const QJsonObject response = params.value(QStringLiteral("response")).toObject();
            emit networkResponse(params.value(QStringLiteral("requestId")).toString(),
                                 response.value(QStringLiteral("url")).toString(),
                                 response.value(QStringLiteral("mimeType")).toString());
        } else if (method == QLatin1String("Network.loadingFinished")) {
            emit networkFinished(params.value(QStringLiteral("requestId")).toString());
        }
        return;
    }
    const int id = obj.value(QStringLiteral("id")).toInt();
    if (id == 0)
        return;
    if (m_pendingBodies.contains(id)) {
        const QString requestId = m_pendingBodies.take(id);
        if (obj.contains(QStringLiteral("error"))) {
            emit responseBody(requestId, QByteArray(), false);
            return;
        }
        const QJsonObject result = obj.value(QStringLiteral("result")).toObject();
        QByteArray body = result.value(QStringLiteral("body")).toString().toUtf8();
        if (result.value(QStringLiteral("base64Encoded")).toBool())
            body = QByteArray::fromBase64(body);
        emit responseBody(requestId, body, !body.isEmpty());
        return;
    }
    if (id == m_pendingShot) {
        m_pendingShot = 0;
        if (obj.contains(QStringLiteral("error"))) {
            emit screenshotReady(QByteArray(), false);
            return;
        }
        const QByteArray png =
            QByteArray::fromBase64(obj.value(QStringLiteral("result")).toObject().value(QStringLiteral("data")).toString().toLatin1());
        emit screenshotReady(png, !png.isEmpty());
        return;
    }
    if (id == m_pendingEnable || id == m_pendingClose) {
        if (id == m_pendingEnable)
            m_pendingEnable = 0;
        if (id == m_pendingClose)
            m_pendingClose = 0;
        return;
    }
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
