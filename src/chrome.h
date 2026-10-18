#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace zappe {

enum class ChromeFace { Hidden, Player };

struct ChromeLaunch {
    QString binary;
    QString profileDir;
    int port = 9222;
    ChromeFace face = ChromeFace::Hidden;
    QStringList arguments;
};

ChromeLaunch chromeLaunch(const QString &dataDir, ChromeFace face = ChromeFace::Hidden);
QString findChromeBinary();

class ChromeSession : public QObject {
    Q_OBJECT
public:
    explicit ChromeSession(QObject *parent = nullptr);
    bool running() const;
    int port() const { return m_launch.port; }
    ChromeFace face() const { return m_face; }
    void ensure(const QString &dataDir);
    // Janela kiosk, só enquanto o vídeo está na frente.
    void present(const QString &dataDir);
    // Fecha a janela visível e sobe de novo sem janela.
    void relaunchHidden();
    void stop();

signals:
    void ready();
    void failed(const QString &message);
    void exited();

private:
    void startProcess();

    ChromeLaunch m_launch;
    QProcess *m_process = nullptr;
    QString m_dataDir;
    ChromeFace m_face = ChromeFace::Hidden;
    bool m_relaunchHidden = false;
};

} // namespace zappe
