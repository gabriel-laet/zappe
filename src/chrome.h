#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace zappe {

struct ChromeLaunch {
    QString binary;
    QString profileDir;
    int port = 9222;
    QStringList arguments;
};

ChromeLaunch chromeLaunch(const QString &dataDir);
QString findChromeBinary();

class ChromeSession : public QObject {
    Q_OBJECT
public:
    explicit ChromeSession(QObject *parent = nullptr);
    bool running() const;
    int port() const { return m_launch.port; }
    void ensure(const QString &dataDir);
    void stop();

signals:
    void ready();
    void failed(const QString &message);
    void exited();

private:
    ChromeLaunch m_launch;
    QProcess *m_process = nullptr;
};

} // namespace zappe
