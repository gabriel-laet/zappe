#include "chrome.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <cstdio>

namespace zappe {

QString chromeCatalogUserAgent()
{
    return QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                          "(KHTML, like Gecko) Chrome/154.0.0.0 Safari/537.36");
}

QString findOnPath(const QStringList &names)
{
    if (names.isEmpty())
        return {};
    const QString path = qEnvironmentVariable("PATH");
    for (const QString &dir : path.split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
        for (const QString &name : names) {
            const QString candidate = QDir(dir).filePath(name);
            if (QFile::exists(candidate))
                return candidate;
        }
    }
    return {};
}

QString findXvfbBinary()
{
    if (const QString explicitBin = qEnvironmentVariable("XVFB_PATH"); !explicitBin.isEmpty() && QFile::exists(explicitBin))
        return explicitBin;
    return findOnPath({QStringLiteral("Xvfb")});
}

QString bareDisplay(const QString &display)
{
    QString number = display.trimmed();
    if (number.startsWith(QLatin1Char(':')))
        number = number.mid(1);
    return number.section(QLatin1Char('.'), 0, 0);
}

QString chromeCatalogDisplay(const QString &guideDisplay)
{
    QString chosen = qEnvironmentVariable("ZAPPE_CATALOG_DISPLAY").trimmed();
    if (chosen.isEmpty())
        chosen = QStringLiteral("99");
    chosen = bareDisplay(chosen);
    const QString guide = bareDisplay(guideDisplay);
    if (!guide.isEmpty() && chosen == guide) {
        bool ok = false;
        const int number = chosen.toInt(&ok);
        chosen = QString::number(ok && number != 98 ? 98 : 97);
    }
    if (chosen.isEmpty())
        chosen = QStringLiteral("99");
    return QStringLiteral(":") + chosen;
}

QString x11SocketPath(const QString &display)
{
    return QStringLiteral("/tmp/.X11-unix/X") + bareDisplay(display);
}

QProcessEnvironment chromeFaceEnvironment(ChromeFace face, const QProcessEnvironment &base, const QString &guideDisplay,
                                          const QString &guideWayland, const QString &catalogDisplay)
{
    QProcessEnvironment env = base;
    if (face == ChromeFace::Player) {
        if (!guideDisplay.isEmpty())
            env.insert(QStringLiteral("DISPLAY"), guideDisplay);
        if (!guideWayland.isEmpty())
            env.insert(QStringLiteral("WAYLAND_DISPLAY"), guideWayland);
        return env;
    }
    env.insert(QStringLiteral("DISPLAY"), catalogDisplay);
    env.remove(QStringLiteral("WAYLAND_DISPLAY"));
    env.remove(QStringLiteral("WAYLAND_SOCKET"));
    return env;
}

QString findChromeBinary()
{
    if (const QString explicitBin = qEnvironmentVariable("CHROME_PATH"); !explicitBin.isEmpty() && QFile::exists(explicitBin))
        return explicitBin;
    return findOnPath({QStringLiteral("google-chrome-stable"), QStringLiteral("google-chrome"), QStringLiteral("chromium"),
                       QStringLiteral("chromium-browser")});
}

ChromeLaunch chromeLaunch(const QString &dataDir, ChromeFace face)
{
    ChromeLaunch launch;
    launch.binary = findChromeBinary();
    launch.profileDir = QDir(dataDir).filePath(QStringLiteral("chrome-profile"));
    launch.face = face;
    if (const QString port = qEnvironmentVariable("ZAPPE_CDP_PORT"); !port.isEmpty())
        launch.port = port.toInt();
    if (launch.port <= 0)
        launch.port = 9222;
    launch.arguments = {QStringLiteral("--user-data-dir=") + launch.profileDir,
                        QStringLiteral("--remote-debugging-port=") + QString::number(launch.port),
                        QStringLiteral("--remote-allow-origins=*"),
                        QStringLiteral("--no-first-run"),
                        QStringLiteral("--no-default-browser-check"),
                        QStringLiteral("--disable-translate"),
                        QStringLiteral("--autoplay-policy=no-user-gesture-required"),
                        QStringLiteral("--disable-session-crashed-bubble"),
                        QStringLiteral("--disable-features=TranslateUI"),
                        QStringLiteral("--disable-background-timer-throttling"),
                        QStringLiteral("--disable-renderer-backgrounding"),
                        QStringLiteral("--disable-backgrounding-occluded-windows"),
                        QStringLiteral("--hide-crash-restore-bubble"),
                        // Sem isto o headless injeta 800,600 e briga com a janela.
                        QStringLiteral("--ozone-override-screen-size=1920,1080"),
                        QStringLiteral("--window-size=1920,1080")};
    if (face == ChromeFace::Player) {
        // Tela cheia no display do gamescope. O catálogo já foi fechado: o perfil tem lock.
        launch.arguments.append(QStringLiteral("--kiosk"));
    } else {
        // --headless=new anuncia HeadlessChrome e a Netflix/YouTube não montam o catálogo.
        // A janela fica num Xvfb, não no gamescope: posição negativa o gamescope ignora
        // e a janela cobre o guia. Sem GPU real, para não disputar o DRM com o gamescope.
        launch.arguments.append(QStringLiteral("--user-agent=") + chromeCatalogUserAgent());
        launch.arguments.append(QStringLiteral("--disable-blink-features=AutomationControlled"));
        launch.arguments.append(QStringLiteral("--ozone-platform=x11"));
        launch.arguments.append(QStringLiteral("--disable-gpu"));
        launch.arguments.append(QStringLiteral("--mute-audio"));
    }
    launch.arguments.append(QStringLiteral("about:blank"));
    return launch;
}

ChromeSession::ChromeSession(QObject *parent)
    : QObject(parent)
{
}

bool ChromeSession::running() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void ChromeSession::rememberGuideDisplay()
{
    if (m_guideCaptured)
        return;
    m_guideCaptured = true;
    m_guideDisplay = qEnvironmentVariable("DISPLAY");
    m_guideWayland = qEnvironmentVariable("WAYLAND_DISPLAY");
}

bool ChromeSession::ensurePrivateDisplay()
{
    m_catalogDisplay = chromeCatalogDisplay(m_guideDisplay);
    if (!bareDisplay(m_guideDisplay).isEmpty() && bareDisplay(m_catalogDisplay) == bareDisplay(m_guideDisplay)) {
        emit failed(QStringLiteral("Display do catálogo coincide com o do guia."));
        return false;
    }
    const QString socket = x11SocketPath(m_catalogDisplay);
    if (QFile::exists(socket)) {
        std::fprintf(stderr, "zappe: xvfb %s já está no ar\n", qPrintable(m_catalogDisplay));
        return true;
    }
    const QString binary = findXvfbBinary();
    if (binary.isEmpty()) {
        emit failed(QStringLiteral("Xvfb não encontrado. Instale xorg-server-xvfb."));
        return false;
    }
    if (!m_xvfb)
        m_xvfb = new QProcess(this);
    if (m_xvfb->state() == QProcess::NotRunning) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.remove(QStringLiteral("DISPLAY"));
        env.remove(QStringLiteral("WAYLAND_DISPLAY"));
        m_xvfb->setProcessEnvironment(env);
        m_xvfb->setProcessChannelMode(QProcess::ForwardedChannels);
        m_xvfb->setProgram(binary);
        m_xvfb->setArguments({m_catalogDisplay, QStringLiteral("-screen"), QStringLiteral("0"), QStringLiteral("1920x1080x24"),
                              QStringLiteral("-nolisten"), QStringLiteral("tcp"), QStringLiteral("-ac")});
        std::fprintf(stderr, "zappe: xvfb %s %s\n", qPrintable(binary), qPrintable(m_catalogDisplay));
        m_xvfb->start();
    }
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3000) {
        if (QFile::exists(socket))
            return true;
        if (m_xvfb->state() == QProcess::NotRunning)
            break;
        QThread::msleep(50);
    }
    std::fprintf(stderr, "zappe: xvfb %s não abriu %s\n", qPrintable(m_catalogDisplay), qPrintable(socket));
    emit failed(QStringLiteral("Xvfb não abriu o display do catálogo."));
    return false;
}

void ChromeSession::startProcess()
{
    if (m_dataDir.isEmpty())
        return;
    rememberGuideDisplay();
    m_launch = chromeLaunch(m_dataDir, m_face);
    if (m_launch.binary.isEmpty()) {
        emit failed(QStringLiteral("Google Chrome não encontrado (google-chrome-stable)."));
        return;
    }
    if (m_face == ChromeFace::Hidden && !ensurePrivateDisplay())
        return;
    QDir().mkpath(m_launch.profileDir);
    ++m_probeGen;
    m_devtoolsReady = false;
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::ForwardedChannels);
    process->setProcessEnvironment(
        chromeFaceEnvironment(m_face, QProcessEnvironment::systemEnvironment(), m_guideDisplay, m_guideWayland, m_catalogDisplay));
    process->setProgram(m_launch.binary);
    process->setArguments(m_launch.arguments);
    connect(process, &QProcess::started, this, [this] { probeDevTools(0); });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        emit failed(QStringLiteral("Chrome não iniciou."));
    });
    connect(process, &QProcess::finished, this, [this, process] {
        if (m_process == process)
            m_process = nullptr;
        m_devtoolsReady = false;
        ++m_probeGen;
        process->deleteLater();
        emit exited();
        if (!m_relaunchHidden || m_dataDir.isEmpty())
            return;
        m_relaunchHidden = false;
        m_face = ChromeFace::Hidden;
        QTimer::singleShot(250, this, [this] {
            if (!running())
                startProcess();
        });
    });
    if (m_face == ChromeFace::Player) {
        std::fprintf(stderr, "zappe: chrome %s port %d player no gamescope DISPLAY=%s WAYLAND_DISPLAY=%s\n",
                     qPrintable(m_launch.binary), m_launch.port, qPrintable(m_guideDisplay), qPrintable(m_guideWayland));
    } else {
        std::fprintf(stderr, "zappe: chrome %s port %d catálogo em DISPLAY=%s (guia DISPLAY=%s WAYLAND_DISPLAY=%s)\n",
                     qPrintable(m_launch.binary), m_launch.port, qPrintable(m_catalogDisplay), qPrintable(m_guideDisplay),
                     qPrintable(m_guideWayland));
    }
    m_process = process;
    process->start();
}

void ChromeSession::probeDevTools(int attempt)
{
    if (!running())
        return;
    const int generation = m_probeGen;
    const int port = m_launch.port;
    auto *http = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/json/version").arg(port)));
    request.setTransferTimeout(800);
    QNetworkReply *reply = http->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, http, attempt, generation, port] {
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        http->deleteLater();
        if (generation != m_probeGen || !running())
            return;
        if (ok) {
            m_devtoolsReady = true;
            std::fprintf(stderr, "zappe: chrome cdp pronto na porta %d\n", port);
            emit ready();
            return;
        }
        if (attempt >= 40) {
            std::fprintf(stderr, "zappe: chrome cdp não abriu na porta %d\n", port);
            emit failed(QStringLiteral("Chrome não abriu a porta de depuração."));
            return;
        }
        if (attempt == 0 || attempt % 4 == 0)
            std::fprintf(stderr, "zappe: chrome cdp porta %d ainda fechada (tentativa %d)\n", port, attempt + 1);
        QTimer::singleShot(250, this, [this, attempt] { probeDevTools(attempt + 1); });
    });
}

void ChromeSession::ensure(const QString &dataDir)
{
    m_dataDir = dataDir;
    if (running()) {
        if (m_devtoolsReady)
            emit ready();
        return;
    }
    m_relaunchHidden = false;
    m_face = ChromeFace::Hidden;
    startProcess();
}

void ChromeSession::present(const QString &dataDir)
{
    m_dataDir = dataDir;
    m_relaunchHidden = false;
    if (running() && m_face == ChromeFace::Player) {
        if (m_devtoolsReady)
            emit ready();
        return;
    }
    if (running())
        stop();
    m_face = ChromeFace::Player;
    startProcess();
}

void ChromeSession::relaunchHidden()
{
    m_relaunchHidden = true;
    if (!running()) {
        m_relaunchHidden = false;
        m_face = ChromeFace::Hidden;
        startProcess();
        return;
    }
    QTimer::singleShot(2000, this, [this] {
        if (m_relaunchHidden && running())
            stop();
    });
}

void ChromeSession::stop()
{
    if (!m_process)
        return;
    m_process->terminate();
    if (!m_process->waitForFinished(800))
        m_process->kill();
}

} // namespace zappe
