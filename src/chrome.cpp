#include "chrome.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>

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

ChromeLaunch chromeLaunch(const QString &dataDir)
{
    ChromeLaunch launch;
    launch.binary = findChromeBinary();
    launch.profileDir = QDir(dataDir).filePath(QStringLiteral("chrome-profile"));
    if (const QString port = qEnvironmentVariable("ZAPPE_CDP_PORT"); !port.isEmpty())
        launch.port = port.toInt();
    if (launch.port <= 0)
        launch.port = 9222;
    launch.arguments = {QStringLiteral("--user-data-dir=") + launch.profileDir,
                        QStringLiteral("--remote-debugging-port=") + QString::number(launch.port),
                        QStringLiteral("--remote-allow-origins=*"),
                        QStringLiteral("--kiosk"),
                        QStringLiteral("--no-first-run"),
                        QStringLiteral("--no-default-browser-check"),
                        QStringLiteral("--disable-translate"),
                        QStringLiteral("--autoplay-policy=no-user-gesture-required"),
                        QStringLiteral("--disable-session-crashed-bubble"),
                        QStringLiteral("--disable-features=TranslateUI"),
                        QStringLiteral("--disable-background-timer-throttling"),
                        QStringLiteral("--disable-renderer-backgrounding"),
                        QStringLiteral("--disable-backgrounding-occluded-windows"),
                        QStringLiteral("about:blank")};
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

void ChromeSession::ensure(const QString &dataDir)
{
    if (running()) {
        emit ready();
        return;
    }
    m_launch = chromeLaunch(dataDir);
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
    });
    std::fprintf(stderr, "zappe: chrome %s port %d profile %s\n", qPrintable(m_launch.binary), m_launch.port,
                 qPrintable(m_launch.profileDir));
    m_process = process;
    process->start();
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
