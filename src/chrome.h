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
// UA de Chrome normal. HeadlessChrome faz a Netflix e o YouTube não renderizarem o catálogo.
QString chromeCatalogUserAgent();

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
    // Fecha a janela do vídeo e sobe de novo fora da tela.
    void relaunchHidden();
    void stop();

signals:
    void ready();
    void failed(const QString &message);
    void exited();

private:
    void startProcess();
    void probeDevTools(int attempt);

    ChromeLaunch m_launch;
    QProcess *m_process = nullptr;
    QString m_dataDir;
    ChromeFace m_face = ChromeFace::Hidden;
    bool m_relaunchHidden = false;
    bool m_devtoolsReady = false;
    int m_probeGen = 0;
};

} // namespace zappe
