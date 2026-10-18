#include "chrome.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>

namespace zappe {

QString findChromeBinary()
{
    if (const QString explicitBin = qEnvironmentVariable("CHROME_PATH"); !explicitBin.isEmpty() && QFile::exists(explicitBin))
        return explicitBin;
    const QStringList names = {QStringLiteral("google-chrome-stable"), QStringLiteral("google-chrome"),
                               QStringLiteral("chromium"), QStringLiteral("chromium-browser")};
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
                        QStringLiteral("--disable-backgrounding-occluded-windows")};
    if (face == ChromeFace::Player) {
        // Tela cheia só no Play. No gamescope, minimizar não tira a janela da frente.
        launch.arguments.append(QStringLiteral("--kiosk"));
        launch.arguments.append(QStringLiteral("--window-size=1920,1080"));
    } else {
        // Sem janela: o kiosk em about:blank ficava branco por cima do guia.
        launch.arguments.append(QStringLiteral("--headless=new"));
        launch.arguments.append(QStringLiteral("--window-size=1920,1080"));
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

void ChromeSession::startProcess()
{
    if (m_dataDir.isEmpty())
        return;
    m_launch = chromeLaunch(m_dataDir, m_face);
    if (m_launch.binary.isEmpty()) {
        emit failed(QStringLiteral("Google Chrome não encontrado (google-chrome-stable)."));
        return;
    }
    QDir().mkpath(m_launch.profileDir);
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::ForwardedChannels);
    process->setProgram(m_launch.binary);
    process->setArguments(m_launch.arguments);
    connect(process, &QProcess::started, this, &ChromeSession::ready);
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        emit failed(QStringLiteral("Chrome não iniciou."));
    });
    connect(process, &QProcess::finished, this, [this, process] {
        if (m_process == process)
            m_process = nullptr;
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
    std::fprintf(stderr, "zappe: chrome %s port %d %s\n", qPrintable(m_launch.binary), m_launch.port,
                 m_face == ChromeFace::Player ? "player" : "oculto");
    m_process = process;
    process->start();
}

void ChromeSession::ensure(const QString &dataDir)
{
    m_dataDir = dataDir;
    if (running()) {
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
