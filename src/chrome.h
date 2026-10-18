#pragma once

#include <QObject>
#include <QProcess>
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
    void stop();

signals:
    void ready();
    void failed(const QString &message);
    void exited();

private:
    void rememberGuideDisplay();
    bool ensurePrivateDisplay();
    void startProcess();
    void probeDevTools(int attempt);

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
    int m_probeGen = 0;
};

} // namespace zappe
