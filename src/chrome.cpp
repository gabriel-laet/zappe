#include "chrome.h"

#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <cstdio>

namespace zappe {

QString chromeCatalogUserAgent()
{
    return QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                          "(KHTML, like Gecko) Chrome/154.0.0.0 Safari/537.36");
}

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
                        QStringLiteral("--disable-backgrounding-occluded-windows"),
                        // Sem isto o headless injeta 800,600 e briga com a janela.
                        QStringLiteral("--ozone-override-screen-size=1920,1080"),
                        QStringLiteral("--window-size=1920,1080")};
    if (face == ChromeFace::Player) {
        // Tela cheia só no Play. No gamescope, minimizar não tira a janela da frente.
        launch.arguments.append(QStringLiteral("--kiosk"));
    } else {
        // --headless=new anuncia HeadlessChrome e a Netflix/YouTube não montam o catálogo
        // (sem grade, sem o feed browse/graphql). Janela real, fora da tela, sem foco.
        // O gamescope mostra a janela focada; o guia pede o foco de volta.
        launch.arguments.append(QStringLiteral("--user-agent=") + chromeCatalogUserAgent());
        launch.arguments.append(QStringLiteral("--disable-blink-features=AutomationControlled"));
        launch.arguments.append(QStringLiteral("--window-position=-3000,-3000"));
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
    ++m_probeGen;
    m_devtoolsReady = false;
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::ForwardedChannels);
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
    std::fprintf(stderr, "zappe: chrome %s port %d %s\n", qPrintable(m_launch.binary), m_launch.port,
                 m_face == ChromeFace::Player ? "player" : "oculto");
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
