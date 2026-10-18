#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QProcessEnvironment>
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
QString findXvfbBinary();
// UA de Chrome normal. HeadlessChrome faz a Netflix e o YouTube não renderizarem o catálogo.
QString chromeCatalogUserAgent();
// Display X só do catálogo. Não é o display do gamescope.
QString chromeCatalogDisplay(const QString &guideDisplay);
QString x11SocketPath(const QString &display);
// Catálogo: DISPLAY do Xvfb, sem WAYLAND_DISPLAY. Player: o display do gamescope.
QProcessEnvironment chromeFaceEnvironment(ChromeFace face, const QProcessEnvironment &base, const QString &guideDisplay,
                                          const QString &guideWayland, const QString &catalogDisplay);
// "code 0" ou "signal 9". CrashExit do Qt traz o sinal no código.
QString chromeExitCause(int exitCode, int exitStatus);
// Primeira relançada espera 1s; dobra até 30s.
int chromeWatchBackoffMs(int attempt);
// Catálogo que deveria estar no ar, com processo morto ou porta fechada. O player não entra.
bool chromeCatalogDown(bool wanted, ChromeFace face, bool running, bool portOpen);
// Socket sumiu, ou o Xvfb que o app subiu morreu.
bool chromeDisplayDown(bool wanted, bool socketExists, bool tracked, bool xvfbRunning);

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
    // Fecha o kiosk e sobe de novo no Xvfb. O perfil não abre duas instâncias.
    void relaunchHidden();
    // Processo morto ou porta fechada: sobe o catálogo de novo, com espera.
    void reviveCatalog();
    void stop();

signals:
    void ready();
    void failed(const QString &message);
    void exited();

private:
    void rememberGuideDisplay();
    bool ensurePrivateDisplay();
    void restartPrivateDisplay();
    void startProcess();
    void probeDevTools(int attempt);
    void armWatch();
    void checkCatalog();
    void scheduleRevive();
    void reviveNow();

    ChromeLaunch m_launch;
    QProcess *m_process = nullptr;
    QProcess *m_xvfb = nullptr;
    QString m_dataDir;
    QString m_guideDisplay;
    QString m_guideWayland;
    QString m_catalogDisplay;
    ChromeFace m_face = ChromeFace::Hidden;
    bool m_guideCaptured = false;
    bool m_relaunchHidden = false;
    bool m_devtoolsReady = false;
    bool m_catalogWanted = false;
    bool m_stopping = false;
    bool m_revivePending = false;
    bool m_portProbe = false;
    bool m_xvfbStopping = false;
    int m_probeGen = 0;
    int m_watchAttempt = 0;
    int m_portMisses = 0;
    QTimer *m_watch = nullptr;
};

} // namespace zappe
