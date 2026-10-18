#pragma once

#include "catalog.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

namespace zappe {

struct LoginStep {
    QString op;
    QString url;
    QString selector;
    QString field;
    QString prompt;
    bool secret = false;
    bool optional = false;
};

struct LoginService {
    QString id;
    QString name;
    QString homeUrl;
    QString loginUrl;
    QString readySelector;
    QVector<LoginStep> steps;
};

struct WebhookConfig {
    QString url;
    QString token;
    QString inboxFile;
};

struct LoginCatalog {
    QVector<LoginService> services;
    bool ok = false;
    QString error;
};

LoginCatalog parseLoginCatalog(const QByteArray &json);
LoginCatalog loadLoginCatalog();
const LoginService *findService(const LoginCatalog &catalog, const QString &id);
void appendStreamingShelf(CatalogFile &catalog, const LoginCatalog &logins);

WebhookConfig loadWebhookConfig(const QString &dataDir);
bool webhookEnabled(const WebhookConfig &config);
QByteArray webhookBody(const QString &requestId, const QString &service, const QString &field, const QString &prompt,
                       bool secret);
QString inboxValue(const QByteArray &json, const QString &requestId);

class LoginRunner : public QObject {
    Q_OBJECT
public:
    explicit LoginRunner(QObject *parent = nullptr);
    void start(const LoginService &service, int port, const QString &dataDir);
    void submit(const QString &value);
    void cancel();
    void historyBack();
    void dispatchKey(const QString &key, const QString &code, int virtualKey);
    void dispatchAction(const QString &action);

signals:
    void promptRequested(const QString &service, const QString &field, const QString &prompt, bool secret);
    void playing(const QString &url);
    void failed(const QString &message);
    void status(const QString &message);
    void historyResult(bool wentBack);

private:
    void attach();
    void afterReady();
    void runStep();
    void finishHome();
    void ask(const LoginStep &step);
    void pollInbox();

    class CdpClient *m_cdp = nullptr;
    QNetworkAccessManager *m_http = nullptr;
    QTimer *m_inbox = nullptr;
    LoginService m_service;
    WebhookConfig m_webhook;
    QString m_dataDir;
    QString m_requestId;
    int m_port = 9222;
    int m_step = 0;
    int m_waits = 0;
    bool m_active = false;
    bool m_waiting = false;
};

} // namespace zappe
