#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QVector>
#include <QWebSocket>

namespace zappe {

QByteArray cdpCommand(int id, const QString &method, const QJsonObject &params);
QString jsString(const QString &value);
QString fillScript(const QString &selector, const QString &value);
QString clickScript(const QString &selector);
QString existsScript(const QString &selector);
QString profileScript(const QString &name);
QString historyBackScript();
QString historyGoBackScript();
bool chromeCanGoBack(int currentIndex);

struct PlayerKeyEvent {
    QString type;
    QString key;
    QString code;
    int virtualKey = 0;
};

// Sequência crua + down + up. Netflix ignora só o keyDown.
QVector<PlayerKeyEvent> playerKeyEvents(const QString &action);

class CdpClient : public QObject {
    Q_OBJECT
public:
    explicit CdpClient(QObject *parent = nullptr);

    void attach(int port);
    void navigate(const QString &url);
    void evaluate(const QString &expression);
    void historyBack();
    void dispatchKey(const QString &key, const QString &code, int virtualKey);
    void dispatchAction(const QString &action);
    void enableNetwork();
    void getResponseBody(const QString &requestId);
    void captureScreenshot();
    void closeBrowser();
    void dispatchMouseClick(int x, int y);
    bool connected() const { return m_connected; }

signals:
    void attached();
    void failed(const QString &message);
    void navigated();
    void evaluated(const QString &value, bool ok);
    void historyResult(bool wentBack);
    void networkResponse(const QString &requestId, const QString &url, const QString &mimeType);
    void networkFinished(const QString &requestId);
    void responseBody(const QString &requestId, const QByteArray &body, bool ok);
    void screenshotReady(const QByteArray &png, bool ok);

private:
    void send(const QString &method, const QJsonObject &params);
    void sendHistoryEval(const QString &expression);
    void onMessage(const QString &message);
    void openSocket(const QString &url);

    QWebSocket *m_socket = nullptr;
    QNetworkAccessManager *m_http = nullptr;
    int m_port = 9222;
    int m_nextId = 1;
    int m_pendingNav = 0;
    int m_pendingEval = 0;
    int m_pendingHistory = 0;
    int m_pendingShot = 0;
    int m_pendingEnable = 0;
    int m_pendingClose = 0;
    int m_historyStage = 0;
    QHash<int, QString> m_pendingBodies;
    bool m_connected = false;
};

} // namespace zappe
