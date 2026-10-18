#include "audio.h"
#include "branding.h"
#include "catalog.h"
#include "guide.h"
#include "house.h"
#include "logins.h"
#include "ota.h"
#include "selfcheck.h"

#include <QCommandLineParser>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QTimer>

#include <cstdio>

namespace {

bool hasArg(int argc, char **argv, const char *flag)
{
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], flag) == 0)
            return true;
    }
    return false;
}

bool envOn(const char *name)
{
    const QByteArray value = qgetenv(name);
    return !value.isEmpty() && value != "0" && value != "false" && value != "off";
}

} // namespace

int main(int argc, char **argv)
{
    const bool selfCheck = hasArg(argc, argv, "--self-check");
    if (selfCheck && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    bool eglfs = qEnvironmentVariable("QT_QPA_PLATFORM") == QByteArray("eglfs");
    for (int i = 1; i < argc && !eglfs; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "eglfs" || arg == "eglfs_kms")
            eglfs = true;
    }
    if (eglfs)
        qputenv("QT_QPA_EGLFS_HIDECURSOR", "1");

    // Alder Lake-N (N150) presents through Mesa EGL. Pin the Quick scene graph
    // to OpenGL so Wayland and `-platform eglfs` share one path. ZAPPE_SOFTWARE=1
    // is only for headless frame grabs.
    if (envOn("ZAPPE_SOFTWARE")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    } else {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setSwapInterval(1);
        format.setDepthBufferSize(0);
        format.setStencilBufferSize(8);
        QSurfaceFormat::setDefaultFormat(format);
    }

    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("zappe"));
    QCoreApplication::setApplicationName(QStringLiteral("zappe"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Zappe — guia de sala em Qt Quick"));
    parser.addHelpOption();
    parser.addOption(QCommandLineOption(QStringLiteral("self-check"),
                                         QStringLiteral("Parse the sample catalog and branding rules, then exit.")));
    parser.addOption(QCommandLineOption(QStringLiteral("windowed"),
                                         QStringLiteral("Open a 1920x1080 window instead of fullscreen.")));
    parser.addOption(QCommandLineOption(QStringList{QStringLiteral("screenshot")},
                                         QStringLiteral("Grab one frame to <file> and exit."),
                                         QStringLiteral("file")));
    parser.addOption(QCommandLineOption(QStringLiteral("detail"), QStringLiteral("Open the focused title.")));
    parser.addOption(QCommandLineOption(QStringList{QStringLiteral("shelf")},
                                         QStringLiteral("Initial shelf index."), QStringLiteral("n"),
                                         QStringLiteral("0")));
    parser.addOption(QCommandLineOption(QStringList{QStringLiteral("index")},
                                         QStringLiteral("Initial card index."), QStringLiteral("n"),
                                         QStringLiteral("0")));
    parser.addOption(QCommandLineOption(QStringList{QStringLiteral("catalog")},
                                         QStringLiteral("Catalog JSON path. Overrides ZAPPE_CATALOG and the data dir."),
                                         QStringLiteral("file")));
    parser.addOption(QCommandLineOption(QStringLiteral("menu"), QStringLiteral("Open the side menu.")));
    parser.addOption(QCommandLineOption(QStringLiteral("settings"), QStringLiteral("Open Ajustes.")));
    parser.addOption(QCommandLineOption(QStringList{QStringLiteral("page")},
                                         QStringLiteral("Initial page: home, live, streaming, or settings."),
                                         QStringLiteral("name")));
    parser.addOption(QCommandLineOption(QStringLiteral("volume"), QStringLiteral("Show the volume HUD.")));
    parser.addOption(QCommandLineOption(QStringList{QStringLiteral("toast")},
                                         QStringLiteral("Show a toast with <text>."), QStringLiteral("text")));
    parser.process(app);

    if (parser.isSet(QStringLiteral("self-check")))
        return zappe::runSelfCheck();

    if (!envOn("ZAPPE_CURSOR"))
        QGuiApplication::setOverrideCursor(Qt::BlankCursor);

    const QString dataDir = zappe::resolveDataDir();
    const zappe::Branding branding = zappe::loadBranding(dataDir);
    const QString catalogPath = zappe::resolveCatalogPath(parser.value(QStringLiteral("catalog")), dataDir);
    if (!QFile::exists(catalogPath)) {
        std::fprintf(stderr, "zappe: catalog not found: %s\n", qPrintable(catalogPath));
        return 1;
    }

    zappe::CatalogFile catalog = zappe::loadCatalogFile(catalogPath);
    zappe::appendStreamingShelf(catalog, zappe::loadLoginCatalog());
    zappe::mergeLiveChannels(catalog, zappe::listOtaChannels());
    zappe::decorateCatalog(catalog, dataDir, zappe::cacheDir());

    const bool screenshot = parser.isSet(QStringLiteral("screenshot"));
    const bool windowed = parser.isSet(QStringLiteral("windowed")) || envOn("ZAPPE_WINDOWED") || screenshot;

    zappe::Guide guide;
    guide.setApplianceDir(dataDir);
    guide.setCoalesceInput(true);
    guide.setBranding(branding);
    guide.setCatalog(std::move(catalog));
    guide.setChrome(windowed, screenshot, screenshot || envOn("ZAPPE_SKIP_SPLASH"), envOn("ZAPPE_FPS"));
    guide.setInitial(parser.value(QStringLiteral("shelf")).toInt(), parser.value(QStringLiteral("index")).toInt(),
                     parser.isSet(QStringLiteral("detail")));
    QString page = parser.value(QStringLiteral("page"));
    if (parser.isSet(QStringLiteral("settings")))
        page = QStringLiteral("settings");
    guide.setPreview(parser.isSet(QStringLiteral("menu")), page, parser.isSet(QStringLiteral("volume")),
                     parser.value(QStringLiteral("toast")));

    std::fprintf(stderr, "zappe: platform %s\n", qPrintable(QGuiApplication::platformName()));
    std::fprintf(stderr, "zappe: catalog %s\n", qPrintable(guide.catalogPath()));
    std::fprintf(stderr, "zappe: branding %s (%s)\n", qPrintable(branding.name), qPrintable(branding.source));

    zappe::House house;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("guide"), &guide);
    engine.rootContext()->setContextProperty(QStringLiteral("house"), &house);
    // Qt 6.4 embeds the module at `:/Guide/`. Qt 6.5+ uses `:/qt/qml/Guide/`.
    const QString qmlPath = QFile::exists(QStringLiteral(":/Guide/Main.qml"))
                                ? QStringLiteral("qrc:/Guide/Main.qml")
                                : QStringLiteral("qrc:/qt/qml/Guide/Main.qml");
    engine.load(QUrl(qmlPath));
    if (engine.rootObjects().isEmpty()) {
        std::fprintf(stderr, "zappe: QML failed to load\n");
        return 1;
    }
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!window)
        return 1;
    guide.attachWindow(window);
    QObject::connect(&guide, &zappe::Guide::playRequested, &house, &zappe::House::playRequested);
    house.start(&guide, window, dataDir);
    zappe::applyHdmiAudio();

    if (screenshot) {
        const QString path = parser.value(QStringLiteral("screenshot"));
        QTimer::singleShot(1200, window, [window, path] {
            const QImage image = window->grabWindow();
            if (image.isNull() || !image.save(path)) {
                std::fprintf(stderr, "zappe: screenshot failed: %s\n", qPrintable(path));
                QCoreApplication::exit(1);
                return;
            }
            std::fprintf(stderr, "zappe: wrote %s (%dx%d)\n", qPrintable(path), image.width(),
                         image.height());
            QCoreApplication::quit();
        });
    }

    return QGuiApplication::exec();
}
