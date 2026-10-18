#include "logiccheck.h"

#include "audio.h"
#include "catalog.h"
#include "cdp.h"
#include "chrome.h"
#include "display.h"
#include "guide.h"
#include "house.h"
#include "keymap.h"
#include "learn.h"
#include "logins.h"
#include "mirror.h"
#include "ota.h"
#include "posters.h"
#include "remote.h"
#include "samsung.h"
#include "util.h"
#include "voice.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QImage>
#include <QJSEngine>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QBuffer>
#include <QProcess>
#include <QTemporaryDir>

#include <cstdio>

namespace zappe {
namespace {

int fail(const char *message)
{
    std::fprintf(stderr, "logic-check: %s\n", message);
    return 1;
}

} // namespace

int runLogicSelfCheck()
{
    if (actionForEvdev(116) != RemoteAction::Power)
        return fail("power 116 toggles the TV");
    if (!shouldGrabEvdevNode(false, true, false) || !shouldGrabEvdevNode(true, false, false))
        return fail("power node and air mouse stay grabbed");
    if (shouldGrabEvdevNode(false, true, true) || shouldGrabEvdevNode(false, false, false))
        return fail("keyboard node is not grabbed");
    if (actionForEvdev(164) != RemoteAction::PlayPause)
        return fail("play");
    if (actionForEvdev(103) != RemoteAction::Up || actionForEvdev(108) != RemoteAction::Down
        || actionForEvdev(105) != RemoteAction::Left || actionForEvdev(106) != RemoteAction::Right)
        return fail("arrows");
    if (actionForEvdev(28) != RemoteAction::Ok || actionForEvdev(158) != RemoteAction::Back
        || actionForEvdev(14) != RemoteAction::Back || actionForEvdev(1) != RemoteAction::Back)
        return fail("ok/home/del/escape");
    if (actionForEvdev(127) != RemoteAction::Menu || actionForEvdev(104) != RemoteAction::PageUp
        || actionForEvdev(109) != RemoteAction::PageDown)
        return fail("menu/page");
    if (actionForEvdev(115) != RemoteAction::VolumeUp || actionForEvdev(114) != RemoteAction::VolumeDown
        || actionForEvdev(113) != RemoteAction::Mute || actionForEvdev(582) != RemoteAction::Voice)
        return fail("volume/mic");
    if (actionForEvdev(172) != RemoteAction::Unmapped)
        return fail("old homepage code is not home");

    QFile mapFile(QStringLiteral(":/zappe/data/atongx-map.json"));
    if (!mapFile.open(QIODevice::ReadOnly))
        return fail("map resource");
    QString error;
    const QVector<MapBinding> bindings = loadMapBindings(mapFile.readAll(), &error);
    if (bindings.size() < 16)
        return fail("map bindings");
    for (const MapBinding &binding : bindings) {
        if (QString::fromLatin1(actionName(actionForEvdev(binding.code))) != binding.action)
            return fail("map json disagrees with keymap");
    }

    QFile loginFile(QStringLiteral(":/zappe/data/logins.json"));
    if (!loginFile.open(QIODevice::ReadOnly))
        return fail("logins resource");
    const LoginCatalog logins = parseLoginCatalog(loginFile.readAll());
    if (!logins.ok)
        return fail("logins parse");
    const QStringList ids = {QStringLiteral("netflix"), QStringLiteral("youtube"), QStringLiteral("prime"),
                             QStringLiteral("hbo"),    QStringLiteral("apple"),   QStringLiteral("disney"),
                             QStringLiteral("globoplay")};
    if (logins.services.size() != ids.size())
        return fail("seven streamings");
    for (const QString &id : ids) {
        const LoginService *service = findService(logins, id);
        if (!service || !service->homeUrl.startsWith(QStringLiteral("https://")))
            return fail("service url");
        bool asked = false;
        for (const LoginStep &step : service->steps) {
            if (step.op == QLatin1String("ask")
                && (step.field == QLatin1String("email") || step.field == QLatin1String("password")
                    || step.field == QLatin1String("code") || step.field == QLatin1String("profile")))
                asked = true;
        }
        if (!asked)
            return fail("service has no prompt step");
    }

    const QByteArray body = webhookBody(QStringLiteral("req-1"), QStringLiteral("netflix"), QStringLiteral("email"),
                                        QStringLiteral("E-mail"), false);
    if (!body.contains("req-1") || !body.contains("netflix") || !body.contains("email"))
        return fail("webhook body");
    WebhookConfig empty;
    if (webhookEnabled(empty))
        return fail("empty webhook");
    empty.url = QStringLiteral("https://example.invalid/hook");
    if (!webhookEnabled(empty))
        return fail("webhook on");
    if (inboxValue(QByteArrayLiteral("{\"requestId\":\"req-1\",\"value\":\"ada@example.com\"}"), QStringLiteral("req-1"))
        != QLatin1String("ada@example.com"))
        return fail("inbox");
    if (!inboxValue(QByteArrayLiteral("{\"requestId\":\"other\",\"value\":\"x\"}"), QStringLiteral("req-1")).isEmpty())
        return fail("inbox mismatch");

    const QString conf = QStringLiteral("[TV GLOBO]\nDELIVERY_SYSTEM = ISDBT\n[Globo HD]\nDELIVERY_SYSTEM = ISDBT\n"
                                        "[Globo 1Seg]\nDELIVERY_SYSTEM = ISDBT\n[Record]\nDELIVERY_SYSTEM = ISDBT\n"
                                        "[Record HD]\nDELIVERY_SYSTEM = ISDBT\n[SBT]\nDELIVERY_SYSTEM = ISDBT\n"
                                        "[SBT HD]\nDELIVERY_SYSTEM = ISDBT\n[Band 1-seg]\nDELIVERY_SYSTEM = ISDBT\n");
    const QStringList names = parseChannelNames(conf);
    const QStringList playable = filterAndSortChannels(names);
    if (playable.contains(QStringLiteral("Globo 1Seg")) || playable.contains(QStringLiteral("Band 1-seg")))
        return fail("1seg filtered");
    if (resolveChannelName(QStringLiteral("Globo"), names) != QLatin1String("Globo HD"))
        return fail("globo hd");
    if (resolveChannelName(QStringLiteral("Record"), names) != QLatin1String("Record HD"))
        return fail("record hd");
    if (resolveChannelName(QStringLiteral("SBT"), names) != QLatin1String("SBT HD"))
        return fail("sbt hd");
    const QString script = otaPipelineScript(QStringLiteral("/tmp/channels.conf"), QStringLiteral("Globo HD"));
    if (!script.contains(QStringLiteral("dvbv5-zap")) || !script.contains(QStringLiteral("mpv"))
        || !script.contains(QStringLiteral("ESC quit")) || !script.contains(QStringLiteral("BACK quit"))
        || !script.contains(QStringLiteral("GO_BACK quit")) || !script.contains(QStringLiteral("0x21FF26 quit"))
        || !script.contains(QStringLiteral("pkill -TERM -P")) || !script.contains(QStringLiteral("exec setsid")))
        return fail("ota pipeline");
    if (otaFrameIntervalMs() != 3 * 60 * 1000)
        return fail("ota frame interval");
    QTemporaryDir frameDir;
    if (!frameDir.isValid())
        return fail("ota frame dir");
    const QString framePath = otaFramePath(frameDir.path(), QStringLiteral("Globo HD"));
    QDir().mkpath(QFileInfo(framePath).absolutePath());
    QImage shot(32, 18, QImage::Format_RGB32);
    shot.fill(Qt::red);
    if (!shot.save(framePath, "JPG"))
        return fail("ota frame save");
    const QDateTime frameNow = QDateTime::currentDateTime();
    if (!otaFrameIsFresh(framePath, frameNow))
        return fail("fresh ota frame");
    RowData liveRow;
    liveRow.title = QStringLiteral("Globo HD");
    liveRow.live = true;
    assignOtaFrame(liveRow, frameDir.path(), frameNow);
    if (liveRow.artworkSpec != framePath || !liveRow.frame)
        return fail("ota frame assigned");
    QFile frameFile(framePath);
    if (!frameFile.open(QIODevice::ReadWrite))
        return fail("ota frame touch");
    frameFile.setFileTime(frameNow.addDays(-2), QFileDevice::FileModificationTime);
    frameFile.close();
    if (otaFrameIsFresh(framePath, frameNow))
        return fail("stale ota frame");
    const QString artDir = frameDir.filePath(QStringLiteral("art"));
    const ArtPaths card = makeChannelCard(artDir, QStringLiteral("Globo HD"));
    const ArtPaths poster = makePoster(artDir, QStringLiteral("Globo HD"), QStringLiteral("TV aberta"), true);
    const QImage cardImage(card.artwork);
    const QImage posterImage(poster.artwork);
    if (cardImage.isNull() || cardImage.size() != QSize(960, 540) || card.artwork == poster.artwork)
        return fail("channel card");
    if (cardImage.pixel(80, 80) == posterImage.pixel(80, 80) && cardImage.pixel(480, 270) == posterImage.pixel(480, 270))
        return fail("channel card matches the generic poster");

    if (parseVoicePtBr(QStringLiteral("abrir Netflix")).service != QLatin1String("netflix"))
        return fail("voice netflix");
    if (parseVoicePtBr(QStringLiteral("mudo")).kind != VoiceKind::Mute)
        return fail("voice mute");
    if (parseVoicePtBr(QStringLiteral("volume mais")).delta != 1)
        return fail("voice volume");
    const VoiceIntent globo = parseVoicePtBr(QStringLiteral("ir para Globo"));
    if (globo.kind != VoiceKind::Ota || globo.query != QLatin1String("globo"))
        return fail("voice globo");
    if (parseVoicePtBr(QStringLiteral("abrir Globoplay")).service != QLatin1String("globoplay"))
        return fail("voice globoplay");
    if (parseVoicePtBr(QStringLiteral("sincronizar")).ok() || parseVoicePtBr(QStringLiteral("conectar")).ok())
        return fail("harvest and companion phrases are gone");

    const HdmiAudio audio = defaultHdmiAudio();
    const QStringList audioArgs = pactlProfileArgs(audio);
    if (audio.card != QLatin1String("alsa_card.pci-0000_00_1f.3")
        || audio.profile != QLatin1String("output:hdmi-stereo") || !audioArgs.contains(audio.card))
        return fail("hdmi profile");

    if (normalizeResolution(QStringLiteral("nope")) != QLatin1String("1080p"))
        return fail("default resolution");
    if (resolutionSize(QStringLiteral("4K")) != QSize(3840, 2160) || resolutionSize(QStringLiteral("1080p")) != QSize(1920, 1080))
        return fail("resolution size");
    QTemporaryDir dir;
    if (!dir.isValid() || !saveResolution(dir.path(), QStringLiteral("4K"))
        || loadResolution(dir.path()) != QLatin1String("4K"))
        return fail("display.json");

    const SamsungConfig samsung = loadSamsungConfig(dir.path());
    if (samsung.host != QLatin1String("192.168.3.6") || samsung.name != QLatin1String("Zappe"))
        return fail("samsung default");
    const QByteArray payload = samsungKeyPayload(TvKey::VolumeUp);
    if (!payload.contains("KEY_VOLUP") || !payload.contains("ms.remote.control"))
        return fail("samsung payload");
    const QByteArray powerKey = samsungKeyPayload(TvKey::Power);
    if (!powerKey.contains("KEY_POWER") || powerKey.contains("KEY_SOURCE"))
        return fail("samsung power key");
    if (!samsungKeyPayload(TvKey::Hdmi1).contains("KEY_HDMI1")
        || !samsungKeyPayload(TvKey::Hdmi3).contains("KEY_HDMI3")
        || samsungKeyPayload(TvKey::Hdmi2).contains("KEY_SOURCE"))
        return fail("samsung hdmi key");
    if (QString::fromLatin1(tvKeyCode(TvKey::Mute)) != QLatin1String("KEY_MUTE"))
        return fail("samsung mute code");
    const QString ws = samsungWebsocketUrl(samsung, false);
    if (!ws.startsWith(QStringLiteral("ws://192.168.3.6:8001/")) || !ws.contains(QStringLiteral("name=")))
        return fail("samsung url");
    if (parsePowerState(QStringLiteral("on")) != TvPower::On
        || parsePowerState(QStringLiteral("STANDBY")) != TvPower::Standby
        || parsePowerState(QStringLiteral("ready")) != TvPower::Standby
        || parsePowerState(QStringLiteral("sleep")) != TvPower::Standby
        || parsePowerState(QStringLiteral("nope")) != TvPower::Unknown)
        return fail("power state");
    const TvProbe probe = parseProbeJson(
        QByteArrayLiteral("{\"device\":{\"PowerState\":\"on\",\"modelName\":\"UN43TU7020\"},\"name\":\"Sala\"}"));
    if (probe.power != TvPower::On || probe.model != QLatin1String("UN43TU7020") || probe.name != QLatin1String("Sala"))
        return fail("probe json");
    if (parseProbeJson(QByteArrayLiteral("{\"PowerState\":\"standby\"}")).power != TvPower::Standby)
        return fail("probe top-level power");
    if (parseUpnpVolume(QByteArrayLiteral("<CurrentVolume>17</CurrentVolume>")) != 17
        || parseUpnpVolume(QByteArrayLiteral("<u>nope</u>")) != -1)
        return fail("upnp volume");
    quint8 mac[6] = {};
    if (!parseMac(QStringLiteral("02:11:22:33:44:55"), mac) || mac[0] != 0x02 || mac[5] != 0x55)
        return fail("mac");
    if (parseMac(QStringLiteral("02-11"), mac))
        return fail("short mac");
    const QByteArray magic = wolMagicPacket(mac);
    if (magic.size() != 102)
        return fail("wol length");
    for (int i = 0; i < 6; ++i) {
        if (static_cast<unsigned char>(magic.at(i)) != 0xFF)
            return fail("wol prefix");
    }
    for (int copy = 0; copy < 16; ++copy) {
        for (int i = 0; i < 6; ++i) {
            if (static_cast<unsigned char>(magic.at(6 + copy * 6 + i)) != mac[i])
                return fail("wol body");
        }
    }
    const PowerPlan off = planPowerToggle(TvPower::On, true, true, true);
    if (!off.turnOff || !off.deviceConnect || off.wakeOnLan || off.cec)
        return fail("toggle off is device connect only");
    const PowerPlan standby = planPowerToggle(TvPower::Standby, true, true, true);
    if (standby.turnOff || !standby.deviceConnect || !standby.wakeOnLan || !standby.cec)
        return fail("standby wakes with every method");
    const PowerPlan away = planPowerToggle(TvPower::Unknown, false, true, false);
    if (away.turnOff || !away.deviceConnect || !away.wakeOnLan || away.cec)
        return fail("unreachable still tries wol");
    const PowerPlan unknown = planPowerToggle(TvPower::Unknown, true, true, true);
    if (unknown.turnOff || !unknown.deviceConnect || unknown.wakeOnLan || unknown.cec)
        return fail("reachable unknown is key only");
    const QStringList cec = cecWakeArgs(QStringLiteral("/dev/cec0"));
    if (!cec.contains(QStringLiteral("--image-view-on")) || !cec.contains(QStringLiteral("/dev/cec0"))
        || !cec.contains(QStringLiteral("--playback")))
        return fail("cec args");
    SamsungConfig cecOff;
    cecOff.cec = QStringLiteral("off");
    if (!cecDevicePath(cecOff).isEmpty() || cecDevicePath(SamsungConfig{}) != QLatin1String("/dev/cec0"))
        return fail("cec path");
    QFile samsungFile(dir.path() + QStringLiteral("/samsung.json"));
    if (!samsungFile.open(QIODevice::WriteOnly))
        return fail("samsung json");
    samsungFile.write(QByteArrayLiteral(
        "{\"host\":\"10.1.2.3\",\"mac\":\"AA-BB-CC-DD-EE-FF\",\"name\":\"Zappe\",\"cec\":\"off\"}"));
    samsungFile.close();
    const SamsungConfig stored = loadSamsungConfig(dir.path());
    quint8 storedMac[6] = {};
    if (stored.host != QLatin1String("10.1.2.3") || stored.cec != QLatin1String("off")
        || !parseMac(stored.mac, storedMac) || storedMac[0] != 0xAA || storedMac[5] != 0xFF)
        return fail("samsung.json mac");

    const ChromeLaunch chrome = chromeLaunch(dir.path());
    const QString joined = chrome.arguments.join(QLatin1Char(' '));
    const QString playerJoined = chromeLaunch(dir.path(), ChromeFace::Player).arguments.join(QLatin1Char(' '));
    if (chrome.port != 9222 && qEnvironmentVariable("ZAPPE_CDP_PORT").isEmpty())
        return fail("cdp port");
    if (chrome.face != ChromeFace::Hidden || joined.contains(QStringLiteral("--kiosk"))
        || joined.contains(QStringLiteral("--headless")) || !joined.contains(QStringLiteral("--remote-debugging-port="))
        || !joined.contains(QStringLiteral("chrome-profile")) || joined.contains(QStringLiteral("gamescope")))
        return fail("chrome args");
    if (!joined.contains(QStringLiteral("--ozone-override-screen-size=1920,1080"))
        || !joined.contains(QStringLiteral("--window-size=1920,1080")) || joined.contains(QStringLiteral("800,600")))
        return fail("chrome screen");
    if (joined.contains(QStringLiteral("--window-position="))
        || !joined.contains(QStringLiteral("--disable-blink-features=AutomationControlled"))
        || !joined.contains(QStringLiteral("--hide-crash-restore-bubble"))
        || !joined.contains(QStringLiteral("--ozone-platform=x11")) || !joined.contains(QStringLiteral("--disable-gpu"))
        || !joined.contains(QStringLiteral("--mute-audio"))
        || !joined.contains(QStringLiteral("--user-agent=") + chromeCatalogUserAgent())
        || chromeCatalogUserAgent().contains(QStringLiteral("HeadlessChrome"))
        || joined.contains(QStringLiteral("HeadlessChrome")))
        return fail("chrome catalog window");
    if (chromeCatalogDisplay(QStringLiteral(":1")) != QLatin1String(":99")
        || chromeCatalogDisplay(QStringLiteral(":99")) != QLatin1String(":98")
        || x11SocketPath(QStringLiteral(":99")) != QLatin1String("/tmp/.X11-unix/X99"))
        return fail("catalog display");
    if (chromeWatchBackoffMs(0) != 1000 || chromeWatchBackoffMs(1) != 2000 || chromeWatchBackoffMs(2) != 4000
        || chromeWatchBackoffMs(4) != 16000 || chromeWatchBackoffMs(5) != 30000 || chromeWatchBackoffMs(12) != 30000)
        return fail("chrome watch backoff");
    if (chromeExitCause(0, int(QProcess::NormalExit)) != QLatin1String("code 0")
        || chromeExitCause(9, int(QProcess::CrashExit)) != QLatin1String("signal 9")
        || chromeExitCause(1, int(QProcess::NormalExit)) != QLatin1String("code 1"))
        return fail("chrome exit cause");
    if (!chromeCatalogDown(true, ChromeFace::Hidden, false, false)
        || !chromeCatalogDown(true, ChromeFace::Hidden, true, false)
        || chromeCatalogDown(true, ChromeFace::Hidden, true, true)
        || chromeCatalogDown(true, ChromeFace::Player, false, false)
        || chromeCatalogDown(false, ChromeFace::Hidden, false, false))
        return fail("chrome catalog down");
    if (!chromeDisplayDown(true, false, false, false) || !chromeDisplayDown(true, true, true, false)
        || chromeDisplayDown(true, true, false, false) || chromeDisplayDown(false, false, true, false))
        return fail("chrome display down");
    QProcessEnvironment guideEnv;
    guideEnv.insert(QStringLiteral("DISPLAY"), QStringLiteral(":1"));
    guideEnv.insert(QStringLiteral("WAYLAND_DISPLAY"), QStringLiteral("wayland-0"));
    const QProcessEnvironment catalogEnv =
        chromeFaceEnvironment(ChromeFace::Hidden, guideEnv, QStringLiteral(":1"), QStringLiteral("wayland-0"), QStringLiteral(":99"));
    const QProcessEnvironment playerEnv =
        chromeFaceEnvironment(ChromeFace::Player, guideEnv, QStringLiteral(":1"), QStringLiteral("wayland-0"), QStringLiteral(":99"));
    if (catalogEnv.value(QStringLiteral("DISPLAY")) != QLatin1String(":99") || catalogEnv.contains(QStringLiteral("WAYLAND_DISPLAY")))
        return fail("catalog env");
    if (playerEnv.value(QStringLiteral("DISPLAY")) != QLatin1String(":1")
        || playerEnv.value(QStringLiteral("WAYLAND_DISPLAY")) != QLatin1String("wayland-0"))
        return fail("player env");
    if (!playerJoined.contains(QStringLiteral("--kiosk")) || playerJoined.contains(QStringLiteral("--headless"))
        || playerJoined.contains(QStringLiteral("--ozone-platform=x11"))
        || !playerJoined.contains(QStringLiteral("--hide-crash-restore-bubble")))
        return fail("chrome player");
    if (!playerJoined.contains(QStringLiteral("--ozone-override-screen-size=1920,1080"))
        || !playerJoined.contains(QStringLiteral("--window-size=1920,1080")))
        return fail("chrome player screen");
    QString retryService;
    if (!mirrorShelfRetries(QStringLiteral("error"), QStringLiteral("mirror:netflix"), retryService)
        || retryService != QLatin1String("netflix")
        || mirrorShelfRetries(QStringLiteral("loading"), QStringLiteral("mirror:netflix"), retryService)
        || mirrorShelfRetries(QStringLiteral("error"), QStringLiteral("apps"), retryService))
        return fail("mirror retry");
    if (mirrorLoadTimeoutMs() < 10000 || mirrorLoadTimeoutMs() > 60000)
        return fail("mirror load timeout");
    if (!cdpCommand(7, QStringLiteral("Page.navigate"), QJsonObject{{QStringLiteral("url"), QStringLiteral("https://example")}})
             .contains("Page.navigate"))
        return fail("cdp command");
    if (!fillScript(QStringLiteral("input[type='email']"), QStringLiteral("a@b.c")).contains(QStringLiteral("querySelector")))
        return fail("fill script");
    const QString backScript = historyBackScript();
    if (!backScript.contains(QStringLiteral("history.back")) || !backScript.contains(QStringLiteral("history.length")))
        return fail("chrome history back");
    if (!historyGoBackScript().contains(QStringLiteral("history.back")) || chromeCanGoBack(0) || !chromeCanGoBack(2))
        return fail("chrome history index");
    if (!mpvPlaybackStarted(QByteArrayLiteral("{\"data\":1.25,\"error\":\"success\"}"))
        || mpvPlaybackStarted(QByteArrayLiteral("{\"data\":null,\"error\":\"success\"}"))
        || mpvPlaybackStarted(QByteArrayLiteral("not-json")))
        return fail("mpv playback");
    if (House::loadingTimeoutMs != 10000)
        return fail("loading timeout");
    if (!harvestErrorText(QStringLiteral("no Chrome/Chromium application on the AT-SPI bus")))
        return fail("harvest error text");
    const QJsonObject root = QJsonDocument::fromJson(QByteArray(R"json({
      "shelves": [{
        "id": "continue",
        "title": "Continue watching",
        "status": "error",
        "message": "AT-SPI dump timed out",
        "rows": []
      }]
    })json"))
                                .object();
    QJsonObject editable = root;
    if (!removeHarvestErrors(editable) || !editable.value(QStringLiteral("shelves")).toArray().isEmpty())
        return fail("remove harvest shelf");

    const QVector<MirrorAdapter> mirrors = loadMirrorAdapters();
    const MirrorAdapter *netflix = findMirrorAdapter(mirrors, QStringLiteral("netflix"));
    const MirrorAdapter *youtube = findMirrorAdapter(mirrors, QStringLiteral("youtube"));
    const MirrorAdapter *prime = findMirrorAdapter(mirrors, QStringLiteral("prime"));
    if (mirrors.size() != 7 || !netflix || !youtube || !prime)
        return fail("mirror adapters");
    if (netflix->kind != QLatin1String("netflix") || youtube->kind != QLatin1String("youtube")
        || prime->kind != QLatin1String("generic"))
        return fail("mirror kinds");
    const QString netflixScript = mirrorExtractScript(*netflix);
    if (!netflixScript.contains(QStringLiteral("data-list-context")) || !netflixScript.contains(QStringLiteral("continueWatching"))
        || !netflixScript.contains(QStringLiteral("jbv")) || !netflixScript.contains(QStringLiteral("flatLinks"))
        || !netflixScript.contains(QStringLiteral("JSON.stringify")))
        return fail("netflix extract");
    if (!mirrorExtractScript(*youtube).contains(QStringLiteral("ytd-rich-shelf-renderer")))
        return fail("youtube extract");
    if (mirrorPlayUrl(*netflix, QStringLiteral("https://www.netflix.com/title/80100172"))
        != QStringLiteral("https://www.netflix.com/watch/80100172")
        || mirrorPlayUrl(*netflix, QStringLiteral("https://www.netflix.com/browse?jbv=80100172"))
               != QStringLiteral("https://www.netflix.com/watch/80100172"))
        return fail("netflix watch url");
    if (!mirrorIsPlayable(*netflix, QStringLiteral("https://www.netflix.com/title/80100172"))
        || !mirrorIsPlayable(*netflix, QStringLiteral("https://www.netflix.com/browse?jbv=80100172"))
        || mirrorIsPlayable(*netflix, netflix->homeUrl))
        return fail("netflix playable");
    if (mirrorPlayUrl(*youtube, QStringLiteral("https://youtu.be/abcdefghijk"))
        != QStringLiteral("https://www.youtube.com/watch?v=abcdefghijk"))
        return fail("youtube watch url");
    if (!mirrorIsPlayable(*prime, QStringLiteral("https://www.primevideo.com/watch/abc"))
        || !mirrorIsPlayable(*prime, QStringLiteral("https://www.primevideo.com/detail/0ABC"))
        || mirrorIsPlayable(*prime, prime->homeUrl))
        return fail("generic playable");
    if (youtube->homeUrl.contains(QStringLiteral("/feed/subscriptions")) || !youtube->homeUrl.contains(QStringLiteral("youtube.com"))
        || youtube->extraUrls.isEmpty() || !youtube->extraUrls.at(0).contains(QStringLiteral("/feed/history")))
        return fail("youtube home");
    if (!netflixScript.contains(QStringLiteral("data-src")) || !netflixScript.contains(QStringLiteral("srcset"))
        || !netflixScript.contains(QStringLiteral("backgroundImage")) || !netflixScript.contains(QStringLiteral("synopsis"))
        || !netflixScript.contains(QStringLiteral("indexOf('//') === 0"))
        || !netflixScript.contains(QStringLiteral("parentElement"))
        || !netflixScript.contains(QStringLiteral("shadowRoot"))
        || !netflixScript.contains(QStringLiteral("1x1"))
        || !netflixScript.contains(QStringLiteral("cfg.profile")))
        return fail("cover art extract");
    if (netflixScript.indexOf(QStringLiteral("el.currentSrc")) < 0
        || netflixScript.indexOf(QStringLiteral("el.currentSrc")) > netflixScript.indexOf(QStringLiteral("srcset")))
        return fail("cover prefers loaded image");
    if (!mirrorCoverFetchScript({QStringLiteral("https://example.test/a.jpg")}).contains(QStringLiteral("credentials: 'include'"))
        || !mirrorCoverFetchScript({QStringLiteral("https://example.test/a.jpg")}).contains(QStringLiteral("8192"))
        || !mirrorCoverFetchScript({QStringLiteral("https://example.test/a.jpg")}).contains(QStringLiteral("err:")))
        return fail("cover fetch script");
    if (!coverFetchIsBlocked(QStringLiteral("Failed to fetch")) || coverFetchIsBlocked(QStringLiteral("http 404")))
        return fail("cover fetch stops on cors");
    if (!coverUrlsMatch(QStringLiteral("https://cdn.example/a/box.jpg?r=1"), QStringLiteral("https://cdn.example/a/box.jpg?r=2"))
        || coverUrlsMatch(QStringLiteral("https://cdn.example/a/box.jpg"), QStringLiteral("https://cdn.example/b/other.jpg"))
        || coverUrlsMatch(QStringLiteral("https://is1-ssl.mzstatic.com/image/thumb/Features/v4/aa/source/640x360bb.jpg"),
                          QStringLiteral("https://is1-ssl.mzstatic.com/image/thumb/Features/v4/bb/source/640x360bb.jpg"))
        || coverUrlsMatch(QStringLiteral("https://i.ytimg.com/vi/abcdefghijk/hqdefault.jpg"),
                          QStringLiteral("https://i.ytimg.com/vi/zzzzzzzzzzz/hqdefault.jpg"))
        || !coverUrlsMatch(QStringLiteral("https://is1-ssl.mzstatic.com/image/thumb/Features/v4/aa/source/640x360bb.jpg?a=1"),
                           QStringLiteral("https://is1-ssl.mzstatic.com/image/thumb/Features/v4/aa/source/640x360bb.jpg?a=2")))
        return fail("cover url match");
    const QString amazonToken = coverLooseToken(
        QStringLiteral("https://m.media-amazon.com/images/S/pv-target-images/abc123def4567890xyz.jpg"));
    const QString amazonSized = coverLooseToken(
        QStringLiteral("https://m.media-amazon.com/images/S/pv-target-images/abc123def4567890xyz._SX640_.jpg"));
    const QString amazonOther = coverLooseToken(
        QStringLiteral("https://m.media-amazon.com/images/S/pv-target-images/zzz9876543210abcdef.jpg"));
    const QString hboToken = coverLooseToken(QStringLiteral(
        "https://images.cdn.prd.api.discomax.com/2024/01/aa11bb22cc33dd44ee55ff6677889900.jpeg"));
    if (amazonToken.size() < 16 || amazonToken != amazonSized || amazonToken == amazonOther || hboToken.size() < 16
        || !coverLooseToken(QStringLiteral("https://i.ytimg.com/vi/abcdefghijk/maxresdefault.jpg")).isEmpty())
        return fail("cover token");
    const QString queryA = coverCachePath(dir.path(), QStringLiteral("https://cdn.example/a/box.jpg?r=1"));
    const QString queryB = coverCachePath(dir.path(), QStringLiteral("https://cdn.example/a/box.jpg?token=2"));
    const QString otherArt = coverCachePath(dir.path(), QStringLiteral("https://cdn.example/a/other.jpg"));
    if (queryA != queryB || queryA == otherArt)
        return fail("cover cache key ignores query");
    QVector<ShelfData> linkedShelves(1);
    RowData linkedRow;
    linkedRow.title = QStringLiteral("Dark");
    linkedRow.artworkSpec = QStringLiteral("https://cdn.example/a/box.jpg?r=1");
    linkedShelves[0].rows.append(linkedRow);
    const QString linkedPath = dir.filePath(QStringLiteral("covers/box.jpg"));
    QDir().mkpath(QFileInfo(linkedPath).absolutePath());
    QFile linkedFile(linkedPath);
    if (!linkedFile.open(QIODevice::WriteOnly) || linkedFile.write(QByteArray(64, 'j')) != 64)
        return fail("write linked cover");
    linkedFile.close();
    bool linkedChanged = false;
    const int linkedCards = assignCoverFile(linkedShelves, QStringLiteral("https://cdn.example/a/box.jpg?r=2"), linkedPath,
                                             &linkedChanged);
    if (linkedCards != 1 || !linkedChanged || linkedShelves.at(0).rows.at(0).artwork != linkedPath
        || !fileUrl(linkedShelves.at(0).rows.at(0).artwork).startsWith(QStringLiteral("file://")))
        return fail("cover links card");
    QVector<ShelfData> distinctShelves(1);
    RowData guerraRow;
    guerraRow.title = QStringLiteral("Guerra");
    guerraRow.artworkSpec = QStringLiteral("https://images.cdn.prd.api.discomax.com/2024/01/guerra11bb22cc33dd44.jpg");
    RowData supergirlRow;
    supergirlRow.title = QStringLiteral("Supergirl");
    supergirlRow.artworkSpec = QStringLiteral("https://images.cdn.prd.api.discomax.com/2024/01/super99aa88bb77cc66dd.jpg");
    distinctShelves[0].rows.append(guerraRow);
    distinctShelves[0].rows.append(supergirlRow);
    const QString guerraPath = dir.filePath(QStringLiteral("covers/guerra.jpg"));
    QFile guerraFile(guerraPath);
    if (!guerraFile.open(QIODevice::WriteOnly) || guerraFile.write(QByteArray(64, 'g')) != 64)
        return fail("write distinct cover");
    guerraFile.close();
    if (coverUrlsMatch(guerraRow.artworkSpec, supergirlRow.artworkSpec))
        return fail("different cards share a cover");
    if (assignCoverFile(distinctShelves, guerraRow.artworkSpec, guerraPath, nullptr) != 1
        || distinctShelves.at(0).rows.at(0).artwork != guerraPath
        || distinctShelves.at(0).rows.at(1).artwork == guerraPath)
        return fail("one cover painted every card");
    const QString referer = coverDownloadReferer(QStringLiteral("https://www.netflix.com/browse"));
    if (!referer.contains(QStringLiteral("netflix.com")) || referer.contains(QStringLiteral("nflxso"))
        || !coverDownloadReferer(QStringLiteral("https://www.primevideo.com/")).contains(QStringLiteral("primevideo.com"))
        || !coverDownloadReferer(QStringLiteral("notaurl")).isEmpty())
        return fail("cover referer is the service");
    if (!fileUrl(QStringLiteral("/tmp/zappe-cover.jpg")).startsWith(QStringLiteral("file://")))
        return fail("cover file url");
    if (!coverCachePath(QStringLiteral("/tmp/zappe-cache"), QStringLiteral("https://cdn.example/a.webp"))
             .endsWith(QStringLiteral(".jpg")))
        return fail("cover cache is jpeg");
    const QString hboArt = expandCoverUrl(
        QStringLiteral("https://images.cdn.prd.api.discomax.com/a.jpeg?w={width}&h={height}"));
    const QString appleArt = expandCoverUrl(
        QStringLiteral("https://is1-ssl.mzstatic.com/image/thumb/Features/v4/xx/source/{w}x{h}{c}.{f}"));
    const QString encodedArt = expandCoverUrl(QStringLiteral("https://cdn.example/a.jpg?w=%7Bwidth%7D"));
    const QString amazonArt =
        expandCoverUrl(QStringLiteral("https://m.media-amazon.com/images/I/abc._SX{width}_SY{height}_.jpg"));
    if (hboArt.contains(QLatin1Char('{')) || !hboArt.contains(QStringLiteral("w=640"))
        || !hboArt.contains(QStringLiteral("h=360"))
        || appleArt != QStringLiteral("https://is1-ssl.mzstatic.com/image/thumb/Features/v4/xx/source/640x360bb.jpg")
        || encodedArt.contains(QLatin1Char('{')) || !encodedArt.contains(QStringLiteral("w=640"))
        || !amazonArt.contains(QStringLiteral("._SX640_SY360_")))
        return fail("cover template");
    QImage solid(40, 30, QImage::Format_RGB32);
    solid.fill(Qt::red);
    QByteArray solidJpeg;
    QBuffer solidBuffer(&solidJpeg);
    solidBuffer.open(QIODevice::WriteOnly);
    if (!solid.save(&solidBuffer, "JPEG") || coverJpegFromBody(solidJpeg, nullptr).size() < 32)
        return fail("jpeg cover decodes");
    const QString webpPath = QDir::temp().filePath(QStringLiteral("zappe-cover.webp"));
    QProcess webp;
    webp.start(QStringLiteral("ffmpeg"),
               {QStringLiteral("-y"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                QStringLiteral("error"), QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                QStringLiteral("color=c=red:s=32x24"), QStringLiteral("-frames:v"), QStringLiteral("1"), webpPath});
    if (!webp.waitForStarted(2000) || !webp.waitForFinished(8000) || webp.exitCode() != 0)
        return fail("webp sample");
    QFile webpFile(webpPath);
    if (!webpFile.open(QIODevice::ReadOnly))
        return fail("webp sample open");
    QString webpDetail;
    const QByteArray webpJpeg = coverJpegFromBody(webpFile.readAll(), &webpDetail);
    if (webpJpeg.size() < 32 || static_cast<unsigned char>(webpJpeg.at(0)) != 0xFF
        || static_cast<unsigned char>(webpJpeg.at(1)) != 0xD8)
        return fail("webp cover becomes jpeg");
    QByteArray fakeAvif;
    fakeAvif.append("\x00\x00\x00\x20", 4);
    fakeAvif.append("ftyp");
    fakeAvif.append("mif1");
    fakeAvif.append("\x00\x00\x00\x00", 4);
    fakeAvif.append("mif1");
    fakeAvif.append("avif");
    fakeAvif.append(QByteArray(16, '\0'));
    QString avifDetail;
    const QByteArray avifJpeg = coverJpegFromBody(fakeAvif, &avifDetail, QStringLiteral("image/avif"));
    if (avifJpeg.size() < 32 && !avifDetail.startsWith(QStringLiteral("avif")))
        return fail("avif magic");
    const QString avifPath = QDir::temp().filePath(QStringLiteral("zappe-cover.avif"));
    QProcess avif;
    avif.start(QStringLiteral("ffmpeg"),
               {QStringLiteral("-y"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                QStringLiteral("error"), QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                QStringLiteral("color=c=blue:s=32x24"), QStringLiteral("-frames:v"), QStringLiteral("1"),
                QStringLiteral("-c:v"), QStringLiteral("libaom-av1"), QStringLiteral("-still-picture"),
                QStringLiteral("1"), avifPath});
    if (avif.waitForStarted(2000) && avif.waitForFinished(15000) && avif.exitCode() == 0) {
        QFile avifFile(avifPath);
        if (!avifFile.open(QIODevice::ReadOnly))
            return fail("avif sample open");
        QString avifBodyDetail;
        const QByteArray avifBody = avifFile.readAll();
        const QByteArray avifOut = coverJpegFromBody(avifBody, &avifBodyDetail, QStringLiteral("image/avif"));
        if (avifOut.size() < 32 || static_cast<unsigned char>(avifOut.at(0)) != 0xFF
            || static_cast<unsigned char>(avifOut.at(1)) != 0xD8)
            return fail("avif cover becomes jpeg");
    }
    if (!mirrorCoverClipScript({QStringLiteral("https://cdn.example/a/box.jpg")}).contains(QStringLiteral("getBoundingClientRect"))
        || !mirrorCoverClipScript({QStringLiteral("https://cdn.example/a/box.jpg")}, {QStringLiteral("Dark")})
                .contains(QStringLiteral("aria-label"))
        || !mirrorCoverClipScript({QStringLiteral("https://cdn.example/a/box.jpg")}, {QStringLiteral("Dark")})
                .contains(QStringLiteral("title.length >= 6")))
        return fail("cover clip script");
    if (!coverUrlIsPlaceholder(QStringLiteral("https://tv.apple.com/assets/1x1.gif"))
        || !coverUrlIsPlaceholder(QStringLiteral("https://is1-ssl.mzstatic.com/image/thumb/a/1x1bb.gif"))
        || coverUrlIsPlaceholder(QStringLiteral("https://is1-ssl.mzstatic.com/image/thumb/a/640x360bb.jpg")))
        return fail("ignore 1x1 gif");
    if (mirrorKeepsProfiles(*youtube) || !mirrorKeepsProfiles(*netflix)
        || mirrorProfileLabel(QStringLiteral("Account menu")) || mirrorProfileLabel(QStringLiteral("Menu da conta"))
        || !mirrorProfileLabel(QStringLiteral("Ada")))
        return fail("youtube has no profiles");
    if (!mirrorUrlIsPlayback(QStringLiteral("https://www.netflix.com/watch/80100172"))
        || !mirrorUrlIsPlayback(QStringLiteral("https://www.youtube.com/watch?v=abcdefghijk"))
        || mirrorUrlIsPlayback(netflix->homeUrl) || mirrorUrlIsPlayback(youtube->homeUrl)
        || mirrorUrlIsPlayback(youtube->extraUrls.at(0)))
        return fail("catalog refuses watch urls");
    if (mirrorRowService(QStringLiteral("netflix"), QString()) != QStringLiteral("netflix")
        || !mirrorRowService(QStringLiteral("netflix"), QStringLiteral("https://www.netflix.com/watch/80100172")).isEmpty()
        || !mirrorRowService(QStringLiteral("library"), QString()).isEmpty())
        return fail("local mirror row opens the service");
    if (!mirrorKeepRicherPage(10, 1) || mirrorKeepRicherPage(10, 6) || mirrorKeepRicherPage(3, 1))
        return fail("keep richer youtube page");
    if (!mirrorHoldLoading(0, 0) || mirrorHoldLoading(0, 26) || mirrorHoldLoading(10, 0))
        return fail("hold loading until first read");
    const MirrorAdapter *disney = findMirrorAdapter(mirrors, QStringLiteral("disney"));
    const MirrorAdapter *globoplay = findMirrorAdapter(mirrors, QStringLiteral("globoplay"));
    if (!disney || disney->playerSeek != QLatin1String("script")
        || !disney->cardSelector.contains(QStringLiteral("set-item"))
        || !disney->linkSelector.contains(QStringLiteral("/browse/entity"))
        || !mirrorIsPlayable(*disney, QStringLiteral("https://www.disneyplus.com/browse/entity-abc")) || !globoplay
        || !globoplay->playerClick
        || globoplay->playerOk != QLatin1String("script") || globoplay->playerSeek != QLatin1String("script"))
        return fail("player transport");
    if (!mirrorFocusPlayerScript(*prime).contains(QStringLiteral("mediaKeys"))
        || !mirrorTransportScript(QStringLiteral("right")).contains(QStringLiteral("currentTime")))
        return fail("player video");
    if (!mirrorSearchUrl(*netflix, QStringLiteral("dark")).contains(QStringLiteral("q=dark")))
        return fail("netflix search");
    if (mirrorChromeInFront(false) || !mirrorChromeInFront(true))
        return fail("mirror chrome policy");
    qunsetenv("ZAPPE_MIRROR_REFRESH_MIN");
    if (mirrorRefreshMs() != 45 * 60 * 1000 || mirrorCoverUnusedMaxAgeSecs() != 14 * 24 * 60 * 60)
        return fail("mirror refresh default");
    qputenv("ZAPPE_MIRROR_REFRESH_MIN", "15");
    if (mirrorRefreshMs() != 30 * 60 * 1000)
        return fail("mirror refresh floor");
    qputenv("ZAPPE_MIRROR_REFRESH_MIN", "120");
    if (mirrorRefreshMs() != 60 * 60 * 1000)
        return fail("mirror refresh cap");
    qputenv("ZAPPE_MIRROR_REFRESH_MIN", "40");
    if (mirrorRefreshMs() != 40 * 60 * 1000)
        return fail("mirror refresh env");
    qunsetenv("ZAPPE_MIRROR_REFRESH_MIN");
    MirrorAdapter limited = *netflix;
    limited.limitCards = 2;
    const MirrorPage page = parseMirrorPage(QByteArray(R"json({
      "profiles": ["Ada"],
      "needsLogin": false,
      "shelves": [{
        "title": "Continuar assistindo",
        "context": "continueWatching",
        "rows": [
          {"title": "Dark", "href": "https://www.netflix.com/title/80100172", "artwork": "https://example.test/dark.jpg", "synopsis": "Dois mundos.", "progress": 0.42},
          {"title": "Dois", "href": "https://www.netflix.com/title/2", "artwork": "https://example.test/2.jpg", "progress": 0},
          {"title": "Tres", "href": "https://www.netflix.com/title/3", "artwork": "", "progress": 0}
        ]
      }]
    })json"),
                                                     limited);
    const ShelfData *continued = nullptr;
    for (const ShelfData &shelf : page.shelves) {
        if (shelf.id.endsWith(QStringLiteral(":continue")))
            continued = &shelf;
    }
    if (!page.ok || page.profiles.size() != 1 || !continued || continued->rows.size() != 2
        || continued->rows.at(0).href != QStringLiteral("https://www.netflix.com/watch/80100172")
        || continued->rows.at(0).progress < 0.4 || !continued->rows.at(0).wide
        || continued->rows.at(0).synopsis != QStringLiteral("Dois mundos."))
        return fail("mirror page");
    MirrorAdapter primeFilter = *prime;
    primeFilter.limitCards = 8;
    const MirrorPage paid = parseMirrorPage(QByteArray(R"json({
      "shelves": [{
        "title": "Home",
        "rows": [
          {"title": "Dark", "href": "https://www.primevideo.com/detail/DARK1", "artwork": "https://example.test/dark.jpg"},
          {"title": "Watch with Paramount+", "href": "https://www.primevideo.com/detail/PAID", "artwork": "https://example.test/paid.jpg"}
        ]
      }]
    })json"),
                                                   primeFilter);
    if (!mirrorPageHasTitles(paid) || paid.shelves.isEmpty() || paid.shelves.at(0).rows.size() != 1
        || paid.shelves.at(0).rows.at(0).title != QLatin1String("Dark"))
        return fail("prime paid filter");
    if (!mirrorLiveHero(QStringLiteral("Assistir ao vivo: Copa do Mundo"), QStringLiteral("https://globoplay.globo.com/v/1"))
        || !mirrorLiveHero(QStringLiteral("Ao vivo: Jornal"), QStringLiteral("https://globoplay.globo.com/v/2"))
        || mirrorLiveHero(QStringLiteral("Dark"), QStringLiteral("https://globoplay.globo.com/v/3"))
        || mirrorLiveHero(QStringLiteral("Copa"), QStringLiteral("https://globoplay.globo.com/ao-vivo/copa"))
        || mirrorLiveHero(QStringLiteral("Novela"), QStringLiteral("https://globoplay.globo.com/ao-vivo/novela")))
        return fail("live hero");
    const MirrorPage globoLive = parseMirrorPage(QByteArray(R"json({
      "shelves": [{
        "title": "Continuar assistindo",
        "context": "continuar",
        "rows": [
          {"title": "Assistir ao vivo: Copa do Mundo Feminina Sub-17", "href": "https://globoplay.globo.com/ao-vivo/copa", "artwork": "https://example.test/live.jpg"},
          {"title": "Novela", "href": "https://globoplay.globo.com/v/123", "artwork": "https://example.test/novela.jpg"},
          {"title": "Jornal Nacional", "href": "https://globoplay.globo.com/ao-vivo/jornal", "artwork": "https://example.test/jornal.jpg"}
        ]
      }]
    })json"),
                                                       *globoplay);
    if (!globoplay || globoLive.shelves.isEmpty() || globoLive.shelves.at(0).rows.size() != 2
        || globoLive.shelves.at(0).rows.at(0).title != QLatin1String("Novela")
        || globoLive.shelves.at(0).rows.at(1).title != QLatin1String("Jornal Nacional"))
        return fail("globoplay live hero dropped");
    if (!mirrorIsPlayable(*globoplay, QStringLiteral("https://globoplay.globo.com/novela-x/t/12345"))
        || !globoplay->playPaths.contains(QStringLiteral("/t/")))
        return fail("globoplay title path");
    const MirrorPage globoTitles = parseMirrorPage(QByteArray(R"json({
      "shelves": [{
        "title": "Novelas",
        "rows": [
          {"title": "Assistir ao vivo: Copa", "href": "https://globoplay.globo.com/ao-vivo/copa", "artwork": "https://example.test/live.jpg"},
          {"title": "Novela das nove", "href": "https://globoplay.globo.com/novela-x/t/12345", "artwork": "https://example.test/novela.jpg"},
          {"title": "Jornal Nacional", "href": "https://globoplay.globo.com/v/999", "artwork": "https://example.test/jornal.jpg"}
        ]
      }]
    })json"),
                                                         *globoplay);
    if (globoTitles.shelves.isEmpty() || globoTitles.shelves.at(0).rows.size() != 2
        || globoTitles.shelves.at(0).rows.at(0).title != QLatin1String("Novela das nove")
        || !globoTitles.shelves.at(0).rows.at(0).href.contains(QStringLiteral("/t/12345"))
        || globoTitles.shelves.at(0).rows.at(1).title != QLatin1String("Jornal Nacional"))
        return fail("globoplay keeps title pages");
    if (!mirrorBadgeTitle(QStringLiteral("LIVE")) || !mirrorBadgeTitle(QStringLiteral("Ao vivo"))
        || mirrorBadgeTitle(QStringLiteral("Em alta")) || mirrorBadgeTitle(QStringLiteral("Ao vivo: Jornal")))
        return fail("badge title");
    const QString ytScript = mirrorExtractScript(*youtube);
    const MirrorAdapter *hbo = findMirrorAdapter(mirrors, QStringLiteral("hbo"));
    const MirrorAdapter *apple = findMirrorAdapter(mirrors, QStringLiteral("apple"));
    if (!hbo || !apple || !globoplay)
        return fail("service adapters");
    const QString hboScript = mirrorExtractScript(*hbo);
    const QString globoScript = mirrorExtractScript(*globoplay);
    const QString primeScript = mirrorExtractScript(*prime);
    const QString appleScript = mirrorExtractScript(*apple);
    const QString disneyScript = mirrorExtractScript(*disney);
    if (netflixScript.contains(QStringLiteral("walkShadow")) || netflixScript.contains(QStringLiteral("nestedCards"))
        || netflixScript.contains(QStringLiteral("linksOf")) || netflixScript.contains(QStringLiteral("badgeOnly"))
        || primeScript.contains(QStringLiteral("walkShadow")) || primeScript.contains(QStringLiteral("linksOf"))
        || appleScript.contains(QStringLiteral("nestedCards")) || disneyScript.contains(QStringLiteral("linksOf")))
        return fail("baseline extractor is shared");
    if (!hboScript.contains(QStringLiteral("nestedCards")) || hboScript.contains(QStringLiteral("walkShadow"))
        || hboScript.contains(QStringLiteral("linksOf")))
        return fail("hbo extractor is not isolated");
    if (!ytScript.contains(QStringLiteral("walkShadow")) || ytScript.contains(QStringLiteral("nestedCards"))
        || ytScript.contains(QStringLiteral("linksOf")) || ytScript.contains(QStringLiteral("querySelectorAll('*')")))
        return fail("youtube extractor is not isolated");
    if (!globoScript.contains(QStringLiteral("linksOf")) || !globoScript.contains(QStringLiteral("shadowLinks"))
        || !globoScript.contains(QStringLiteral("depth > 6")) || globoScript.contains(QStringLiteral("walkShadow"))
        || globoScript.contains(QStringLiteral("nestedCards")) || globoScript.contains(QStringLiteral("querySelectorAll('*')")))
        return fail("globoplay extractor is not isolated");
    if (ytScript.contains(QStringLiteral("querySelectorAll('*')")) || !ytScript.contains(QStringLiteral("shadowRoot")))
        return fail("youtube shadow extract");
    if (!mirrorDomReaderFirst(QStringLiteral("netflix")) || mirrorDomReaderFirst(QStringLiteral("prime"))
        || mirrorDomReaderFirst(QStringLiteral("youtube")) || mirrorDomReaderFirst(QStringLiteral("globoplay"))
        || mirrorDomReaderFirst(QStringLiteral("hbo")))
        return fail("netflix dom reader stays first");
    if (mirrorFeedReplacesDom(QStringLiteral("netflix"), 90, 90, 40, 40)
        || !mirrorFeedReplacesDom(QStringLiteral("netflix"), 90, 0, 40, 40)
        || !mirrorFeedReplacesDom(QStringLiteral("netflix"), 0, 0, 40, 0)
        || mirrorFeedReplacesDom(QStringLiteral("prime"), 35, 35, 10, 10)
        || !mirrorFeedReplacesDom(QStringLiteral("prime"), 2, 2, 30, 30))
        return fail("feed must not hide netflix covers");
    const QString clipScript =
        mirrorCoverClipScript({QStringLiteral("https://occ.example/nf/box.jpg")}, {QStringLiteral("Dark")});
    if (!clipScript.contains(QStringLiteral("for (let i = 0; i < light.length; i++) nodes.push(light[i]);"))
        || !clipScript.contains(QStringLiteral("shadowRoot"))
        || clipScript.contains(QStringLiteral("nodes.length < 120")))
        return fail("netflix clip scans every image");
    const auto runDom = [](const QString &base, const QString &tree, const QString &script, QString *error) {
        QJSEngine engine;
        const QString source = QStringLiteral(R"js(
function URL(href, base) {
  href = String(href || '');
  base = String(base || '');
  if (href.indexOf('http://') === 0 || href.indexOf('https://') === 0) this.href = href;
  else if (href.indexOf('//') === 0) this.href = 'https:' + href;
  else if (href.charAt(0) === '/') {
    var origin = /^(https?:\/\/[^\/]+)/.exec(base);
    this.href = (origin ? origin[1] : '') + href;
  } else this.href = href;
}
function matchesSimple(node, simple) {
  if (!node || !node.tagName) return false;
  var i = 0;
  var s = String(simple || '');
  if (!s) return false;
  if (s.charAt(0) !== '#' && s.charAt(0) !== '.' && s.charAt(0) !== '[') {
    var tm = /^[A-Za-z0-9_-]+/.exec(s);
    if (!tm || node.tagName.toLowerCase() !== tm[0].toLowerCase()) return false;
    i = tm[0].length;
  }
  while (i < s.length) {
    var c = s.charAt(i);
    if (c === '#') {
      var idm = /^#([A-Za-z0-9_-]+)/.exec(s.slice(i));
      if (!idm || String(node.getAttribute('id') || '') !== idm[1]) return false;
      i += idm[0].length;
    } else if (c === '.') {
      var cm = /^\.([A-Za-z0-9_-]+)/.exec(s.slice(i));
      var classes = String(node.className || '').split(/\s+/);
      if (!cm || classes.indexOf(cm[1]) < 0) return false;
      i += cm[0].length;
    } else if (c === '[') {
      var am = /^\[([A-Za-z0-9_-]+)(?:(\*|\^)?=(['"])(.*?)\3)?\]/.exec(s.slice(i));
      if (!am) return false;
      var val = node.getAttribute(am[1]);
      if (val === null || val === undefined) return false;
      if (am[2] === '*' && String(val).indexOf(am[4]) < 0) return false;
      if (am[2] === '^' && String(val).indexOf(am[4]) !== 0) return false;
      if (!am[2] && am[4] !== undefined && String(val) !== am[4]) return false;
      i += am[0].length;
    } else return false;
  }
  return true;
}
function matchesCompound(node, sel) {
  var tokens = String(sel || '').trim().split(/\s+/);
  if (!tokens.length || !tokens[0]) return false;
  if (!matchesSimple(node, tokens[tokens.length - 1])) return false;
  var anc = node.parentElement;
  for (var t = tokens.length - 2; t >= 0; t--) {
    var found = false;
    while (anc) {
      if (matchesSimple(anc, tokens[t])) { found = true; anc = anc.parentElement; break; }
      anc = anc.parentElement;
    }
    if (!found) return false;
  }
  return true;
}
function matchesSel(node, sel) {
  var parts = String(sel || '').split(',');
  for (var i = 0; i < parts.length; i++) if (matchesCompound(node, parts[i])) return true;
  return false;
}
function walk(root, sel, out) {
  var kids = root.children || [];
  for (var i = 0; i < kids.length; i++) {
    if (kids[i].matches && kids[i].matches(sel)) out.push(kids[i]);
    walk(kids[i], sel, out);
  }
}
function append(parent, kid) {
  if (!kid || typeof kid !== 'object') return;
  kid.parentElement = parent;
  parent.children.push(kid);
}
function makeEl(tag, attrs, kids) {
  var node = {tagName: String(tag || 'div').toUpperCase(), attrs: attrs || {}, children: [], parentElement: null, style: {}, shadowRoot: null, currentSrc: '', src: ''};
  node.className = node.attrs.class || '';
  node.textContent = node.attrs.text || '';
  node.innerText = node.textContent;
  if (node.attrs.src) node.src = node.attrs.src;
  if (node.attrs.currentSrc) node.currentSrc = node.attrs.currentSrc;
  node.getAttribute = function(name) {
    if (!Object.prototype.hasOwnProperty.call(node.attrs, name)) return null;
    var v = node.attrs[name];
    return v === undefined || v === null ? null : String(v);
  };
  node.matches = function(sel) { return matchesSel(node, sel); };
  node.closest = function(sel) {
    var n = node;
    while (n) { if (n.matches && n.matches(sel)) return n; n = n.parentElement; }
    return null;
  };
  node.querySelectorAll = function(sel) { var out = []; walk(node, sel, out); return out; };
  node.querySelector = function(sel) { var all = node.querySelectorAll(sel); return all.length ? all[0] : null; };
  for (var i = 0; i < (kids || []).length; i++) append(node, kids[i]);
  return node;
}
)js") + QStringLiteral("var location = {href: ")
                                     + jsString(base) + QStringLiteral("};\nvar document = ") + tree
                                     + QStringLiteral(";\ndocument.body = document;\n") + script;
        const QJSValue result = engine.evaluate(source);
        if (result.isError()) {
            if (error)
                *error = result.toString() + QStringLiteral(" line ") + result.property(QStringLiteral("lineNumber")).toString();
            return QString();
        }
        return result.toString();
    };
    const auto catalogOk = [&](const MirrorAdapter &adapter, const QString &base, const QString &tree, int minTitles,
                                const QString &firstShelf, const QStringList &banned, const QString &artHas,
                                const QString &artLacks, const QString &hrefHas, const char *what) {
        QString error;
        const QString json = runDom(base, tree, mirrorExtractScript(adapter), &error);
        const MirrorPage page = parseMirrorPage(json.toUtf8(), adapter);
        int titles = 0;
        QStringList arts;
        QStringList names;
        QStringList shelves;
        QStringList hrefs;
        bool emptyArt = false;
        for (const ShelfData &shelf : page.shelves) {
            if (shelf.id.endsWith(QStringLiteral(":profiles")))
                continue;
            shelves.append(shelf.title);
            for (const RowData &row : shelf.rows) {
                ++titles;
                names.append(row.title);
                hrefs.append(row.href);
                if (!row.artworkSpec.startsWith(QStringLiteral("http")))
                    emptyArt = true;
                arts.append(row.artworkSpec);
            }
        }
        bool dup = false;
        QStringList seenArt;
        for (const QString &art : arts) {
            if (seenArt.contains(art))
                dup = true;
            seenArt.append(art);
        }
        bool bannedHit = false;
        for (const QString &bad : banned) {
            if (names.contains(bad) || shelves.contains(bad))
                bannedHit = true;
        }
        const bool artHasOk = artHas.isEmpty() || arts.join(QLatin1Char(' ')).contains(artHas);
        bool artLacksOk = true;
        if (!artLacks.isEmpty()) {
            for (const QString &art : arts) {
                if (art.contains(artLacks))
                    artLacksOk = false;
            }
        }
        const bool hrefOk = hrefHas.isEmpty() || hrefs.join(QLatin1Char(' ')).contains(hrefHas);
        const bool firstOk = firstShelf.isEmpty() || (!shelves.isEmpty() && shelves.at(0) == firstShelf);
        if (!error.isEmpty() || titles < minTitles || emptyArt || dup || bannedHit || !artHasOk || !artLacksOk || !hrefOk
            || !firstOk) {
            std::fprintf(stderr, "logic-check %s: titles %d shelves %s error %s\n%s\n", what, titles,
                         qPrintable(shelves.join(QStringLiteral(" | "))), qPrintable(error), qPrintable(json.left(280)));
            return false;
        }
        return true;
    };
    const QString netflixTree = QStringLiteral(R"js((function () {
  var card = function (i) {
    return makeEl('a', {href: '/browse?jbv=' + (80000000 + i), 'aria-label': 'Show ' + i}, [
      makeEl('img', {class: 'boxart-image', src: 'https://occ.example/nf/' + i + '.jpg', currentSrc: 'https://occ.example/nf/' + i + '.jpg'}, []),
      makeEl('h3', {text: 'Temporada 4'}, [])
    ]);
  };
  var row = function (ctx, title, start) {
    var cards = [makeEl('h2', {text: title}, [])];
    for (var i = 0; i < 18; i++) cards.push(card(start + i));
    return makeEl('div', {'data-list-context': ctx}, cards);
  };
  return makeEl('div', {}, [
    makeEl('section', {}, [makeEl('h2', {text: 'Só um erro'}, []), makeEl('h3', {text: 'Temporada 4'}, [])]),
    row('new', 'Novidades na Netflix', 0),
    row('continueWatching', 'Continuar assistindo', 18),
    row('trending', 'Em alta', 36),
    row('popular', 'Populares', 54),
    row('again', 'Assistir de novo', 72)
  ]);
})())js");
    if (!catalogOk(*netflix, QStringLiteral("https://www.netflix.com/browse"), netflixTree, 80,
                   QStringLiteral("Novidades na Netflix"),
                   {QStringLiteral("Temporada 4"), QStringLiteral("Só um erro"), QStringLiteral("Poder")},
                   QStringLiteral("occ.example/nf/"), {}, {}, "netflix catalog"))
        return fail("netflix extract regressed");
    const QString primeTree = QStringLiteral(R"js((function () {
  var card = function (i) {
    return makeEl('a', {href: '/detail/PV' + i, 'aria-label': 'Prime ' + i}, [
      makeEl('picture', {}, [
        makeEl('source', {srcset: 'https://m.media-amazon.com/images/S/missing/' + i + '404.jpg'}, []),
        makeEl('img', {src: 'https://m.media-amazon.com/images/S/pv-target-images/p' + i + '.jpg',
          currentSrc: 'https://m.media-amazon.com/images/S/pv-target-images/p' + i + '.jpg'}, [])
      ])
    ]);
  };
  var row = function (id, title, start) {
    var cards = [makeEl('h2', {text: title}, [])];
    for (var i = 0; i < 18; i++) cards.push(card(start + i));
    return makeEl('div', {'data-testid': id}, cards);
  };
  return makeEl('div', {}, [
    row('carousel-top', 'Em destaque', 0),
    row('carousel-more', 'Mais títulos', 18)
  ]);
})())js");
    if (!catalogOk(*prime, QStringLiteral("https://www.primevideo.com/"), primeTree, 30, QStringLiteral("Em destaque"),
                   {}, QStringLiteral("pv-target-images/"), QStringLiteral("missing/"), {}, "prime catalog"))
        return fail("prime extract regressed");
    const QString appleTree = QStringLiteral(R"js((function () {
  var card = function (i) {
    var kind = i % 2 ? '/show/' : '/movie/';
    return makeEl('a', {href: kind + 'apple' + i, 'aria-label': 'Apple ' + i}, [
      makeEl('img', {src: 'https://is1-ssl.mzstatic.com/image/thumb/Features/v4/' + i + '/source/box.jpg',
        currentSrc: 'https://is1-ssl.mzstatic.com/image/thumb/Features/v4/' + i + '/source/box.jpg'}, [])
    ]);
  };
  var row = function (title, start) {
    var cards = [makeEl('h2', {text: title}, [])];
    for (var i = 0; i < 16; i++) cards.push(card(start + i));
    return makeEl('div', {class: 'shelf-row'}, cards);
  };
  return makeEl('div', {}, [row('Apple em alta', 0), row('Apple novidades', 16)]);
})())js");
    if (!catalogOk(*apple, QStringLiteral("https://tv.apple.com/"), appleTree, 25, QStringLiteral("Apple em alta"), {},
                   QStringLiteral("mzstatic.com"), {}, {}, "apple catalog"))
        return fail("apple extract regressed");
    const QString disneyTree = QStringLiteral(R"js((function () {
  var card = function (i) {
    return makeEl('div', {'data-testid': 'set-item'}, [
      makeEl('a', {href: '/browse/entity-' + i, 'aria-label': 'Disney ' + i}, [
        makeEl('img', {src: 'https://disney.images.example/d' + i + '.jpg', currentSrc: 'https://disney.images.example/d' + i + '.jpg'}, [])
      ])
    ]);
  };
  var row = function (title, start) {
    var cards = [makeEl('h2', {text: title}, [])];
    for (var i = 0; i < 16; i++) cards.push(card(start + i));
    return makeEl('div', {'data-testid': 'set'}, cards);
  };
  return makeEl('div', {}, [row('Disney recomendados', 0), row('Disney continua', 16)]);
})())js");
    if (!catalogOk(*disney, QStringLiteral("https://www.disneyplus.com/home"), disneyTree, 24,
                   QStringLiteral("Disney recomendados"), {}, QStringLiteral("disney.images.example"), {}, {},
                   "disney catalog"))
        return fail("disney extract regressed");
    const QString hboTree = QStringLiteral(R"js((function () {
  var tile = function (i) {
    return makeEl('div', {'data-testid': 'tile'}, [
      makeEl('a', {href: '/show/hbo' + i, 'aria-label': 'Hbo ' + i}, [
        makeEl('img', {src: 'https://images.example/hbo/' + i + '.jpg', currentSrc: 'https://images.example/hbo/' + i + '.jpg'}, [])
      ])
    ]);
  };
  var row = function (title, start) {
    var tiles = [makeEl('img', {src: 'https://images.example/hbo/rail-' + start + '.jpg',
      currentSrc: 'https://images.example/hbo/rail-' + start + '.jpg'}, [])];
    for (var i = 0; i < 18; i++) tiles.push(tile(start + i));
    return makeEl('section', {}, [
      makeEl('h2', {text: title}, []),
      makeEl('div', {'data-testid': 'tile-rail'}, tiles)
    ]);
  };
  return makeEl('div', {}, [row('HBO series', 0), row('HBO filmes', 18)]);
})())js");
    if (!catalogOk(*hbo, QStringLiteral("https://play.max.com/"), hboTree, 30, QStringLiteral("HBO series"), {},
                   QStringLiteral("images.example/hbo/"), QStringLiteral("rail-"), {}, "hbo catalog"))
        return fail("hbo extract regressed");
    const QString youtubeTree = QStringLiteral(R"js((function () {
  var item = function (title, id) {
    return makeEl('ytd-rich-item-renderer', {}, [
      makeEl('a', {id: 'video-title-link', href: '/watch?v=' + id, 'aria-label': title}, [
        makeEl('img', {id: 'img', src: 'https://i.ytimg.com/vi/' + id + '/hqdefault.jpg',
          currentSrc: 'https://i.ytimg.com/vi/' + id + '/hqdefault.jpg'}, [])
      ])
    ]);
  };
  var shadowed = [];
  for (var i = 0; i < 18; i++) shadowed.push(item('Video ' + i, 'vid' + i + 'aaaa'));
  var grid = makeEl('ytd-rich-grid-renderer', {}, []);
  grid.shadowRoot = makeEl('div', {}, shadowed);
  return makeEl('div', {}, [
    makeEl('ytd-item-section-renderer', {}, [
      makeEl('h2', {id: 'title', text: 'Em alta'}, []),
      item('MEND_Google', 'mendgoogle1'),
      grid
    ])
  ]);
})())js");
    if (!catalogOk(*youtube, QStringLiteral("https://www.youtube.com/"), youtubeTree, 12, QStringLiteral("Em alta"), {},
                   QStringLiteral("i.ytimg.com"), {}, QStringLiteral("vid5aaaa"), "youtube catalog"))
        return fail("youtube extract regressed");
    const QString globoTree = QStringLiteral(R"js((function () {
  var card = function (i, href, title) {
    return makeEl('div', {class: 'title-card'}, [
      makeEl('a', {href: href, 'aria-label': title}, [
        makeEl('img', {src: 'https://img.example/g/' + i + '.jpg', currentSrc: 'https://img.example/g/' + i + '.jpg'}, [])
      ])
    ]);
  };
  var row = function (title, start, extra) {
    var cards = [makeEl('h2', {text: title}, [])];
    for (var i = 0; i < 16; i++) {
      var n = start + i;
      cards.push(card(n, '/titulo-' + n + '/t/' + (10000 + n), 'Globo ' + n));
    }
    if (extra) cards.push(extra);
    return makeEl('div', {class: 'carousel-row'}, cards);
  };
  return makeEl('div', {}, [
    row('Novelas', 0, card(900, '/ao-vivo/copa', 'Assistir ao vivo: Copa')),
    row('Series', 16, null),
    row('Filmes', 32, null)
  ]);
})())js");
    if (!catalogOk(*globoplay, QStringLiteral("https://globoplay.globo.com/"), globoTree, 40, QStringLiteral("Novelas"),
                   {QStringLiteral("Assistir ao vivo: Copa")}, QStringLiteral("img.example/g/"), {}, QStringLiteral("/t/"),
                   "globoplay catalog"))
        return fail("globoplay extract regressed");
    if (!coverHttpNeedsPage(403) || !coverHttpNeedsPage(401) || coverHttpNeedsPage(404) || coverHttpNeedsPage(200)
        || !mirrorCoverFetchScript({QStringLiteral("https://m.media-amazon.com/images/S/pv-target-images/abc.jpg")})
                .contains(QStringLiteral("credentials: 'include'"))
        || !mirrorCoverClipScript({QStringLiteral("https://cdn.example/a/box.jpg")}, {QStringLiteral("Dark")})
                .contains(QStringLiteral("shadowRoot")))
        return fail("prime 403 uses the page");
    NetworkSample primeSample;
    primeSample.url = QStringLiteral("https://www.primevideo.com/api/enrichItemMetadata");
    primeSample.body = QByteArray(R"json({
      "items": [{
        "title": "A Roda",
        "link": "/detail/RODA1",
        "images": {"packshot": "//m.media-amazon.com/images/S/pv-target-images/abc123def456.jpg"}
      }]
    })json");
    const MirrorPage primeArt = parseMirrorFeeds({primeSample}, *prime);
    if (!mirrorPageHasTitles(primeArt) || primeArt.shelves.isEmpty() || primeArt.shelves.at(0).rows.isEmpty()
        || !primeArt.shelves.at(0).rows.at(0).artworkSpec.contains(QStringLiteral("m.media-amazon.com"))
        || primeArt.shelves.at(0).rows.at(0).artworkSpec.startsWith(QStringLiteral("//")))
        return fail("prime protocol-relative art");
    const MirrorPage youtubeAccount = parseMirrorPage(QByteArray(R"json({
      "profiles": ["Account menu"],
      "shelves": [{
        "title": "Em alta",
        "rows": [{"title": "Dark", "href": "https://www.youtube.com/watch?v=abcdefghijk", "artwork": "https://i.ytimg.com/vi/abcdefghijk/hqdefault.jpg"}]
      }]
    })json"),
                                                      *youtube);
    bool youtubeProfiles = false;
    for (const ShelfData &shelf : youtubeAccount.shelves) {
        if (shelf.id.endsWith(QStringLiteral(":profiles")))
            youtubeProfiles = true;
    }
    if (youtubeProfiles || !mirrorPageHasTitles(youtubeAccount))
        return fail("youtube account menu is not a profile shelf");
    NetworkSample hboSample;
    hboSample.url = QStringLiteral("https://api.hbomax.com/cms/routes/home");
    hboSample.body = QByteArray(R"json({
      "items": [{
        "title": "Show",
        "href": "https://play.max.com/show/abc",
        "image": {
          "uri": "https://api.hbomax.com/missing.jpg",
          "src": "https://images.cdn.prd.api.discomax.com/a.jpeg?w={width}&h={height}"
        }
      }]
    })json");
    const MirrorPage hboPage = parseMirrorFeeds({hboSample}, *hbo);
    if (!mirrorPageHasTitles(hboPage) || hboPage.shelves.isEmpty() || hboPage.shelves.at(0).rows.isEmpty()
        || hboPage.shelves.at(0).rows.at(0).artworkSpec.contains(QStringLiteral("api.hbomax.com"))
        || !hboPage.shelves.at(0).rows.at(0).artworkSpec.contains(QStringLiteral("w=640")))
        return fail("hbo art prefers src");
    MirrorAdapter appleFilter = *apple;
    const MirrorPage appleGif = parseMirrorPage(QByteArray(R"json({
      "shelves": [{
        "title": "Em alta",
        "rows": [{"title": "Filme", "href": "https://tv.apple.com/movie/filme", "artwork": "https://tv.apple.com/assets/1x1.gif"}]
      }]
    })json"),
                                                 appleFilter);
    if (!mirrorPageHasTitles(appleGif) || appleGif.shelves.isEmpty() || appleGif.shelves.at(0).rows.isEmpty()
        || !appleGif.shelves.at(0).rows.at(0).artworkSpec.isEmpty())
        return fail("apple drops 1x1 gif");
    ShelfData focusHome;
    RowData focusDark;
    focusDark.title = QStringLiteral("Dark");
    focusDark.href = QStringLiteral("https://example.test/dark");
    focusHome.rows.append(focusDark);
    ShelfData focusLive;
    RowData focusCopa;
    focusCopa.title = QStringLiteral("Assistir ao vivo: Copa");
    focusCopa.href = QStringLiteral("https://globoplay.globo.com/ao-vivo/copa");
    RowData focusNovela;
    focusNovela.title = QStringLiteral("Novela");
    focusNovela.href = QStringLiteral("https://globoplay.globo.com/v/1?novo=1");
    focusLive.rows.append(focusCopa);
    focusLive.rows.append(focusNovela);
    const QVector<ShelfData> focusShelves{focusHome, focusLive};
    int focusShelf = 0;
    int focusIndex = 0;
    if (!restoreGuideFocus(focusShelves, {1, 1}, QStringLiteral("https://globoplay.globo.com/v/1?velho=1"),
                           QStringLiteral("Novela"), 1, 1, focusShelf, focusIndex)
        || focusShelf != 1 || focusIndex != 1)
        return fail("focus stays on title");
    int jumpedShelf = 0;
    int jumpedIndex = 0;
    if (!restoreGuideFocus(focusShelves, {0, 1}, QString(), QString(), 4, 3, jumpedShelf, jumpedIndex)
        || jumpedShelf != 1 || jumpedIndex != 3)
        return fail("focus does not snap to zero");
    const QString coverPath = coverCachePath(dir.path(), QStringLiteral("https://example.test/dark.jpg"));
    const QDateTime now = QDateTime::currentDateTime();
    if (coverIsFresh(coverPath, now, 3600))
        return fail("missing cover");
    QFile cover(coverPath);
    if (!cover.open(QIODevice::WriteOnly) || cover.write(QByteArray(64, 'x')) != 64)
        return fail("write cover");
    cover.close();
    if (!coverIsFresh(coverPath, now, 3600))
        return fail("fresh cover");
    QDir coverDir(dir.filePath(QStringLiteral("covers")));
    if (!coverDir.mkpath(QStringLiteral(".")))
        return fail("cover dir");
    auto writeAged = [&](const QString &name, int days) {
        const QString path = coverDir.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(QByteArray(40, 'x')) != 40)
            return false;
        file.close();
        QFile aged(path);
        if (!aged.open(QIODevice::ReadWrite))
            return false;
        const bool ok = aged.setFileTime(now.addDays(days), QFileDevice::FileModificationTime);
        aged.close();
        return ok;
    };
    if (!writeAged(QStringLiteral("legacy.img"), -1) || !writeAged(QStringLiteral("stale.jpg"), -20)
        || !writeAged(QStringLiteral("fresh-keep.jpg"), -1))
        return fail("age covers");
    const int purged = purgeStaleCovers(dir.path(), now);
    if (purged < 2 || QFileInfo::exists(coverDir.filePath(QStringLiteral("legacy.img")))
        || QFileInfo::exists(coverDir.filePath(QStringLiteral("stale.jpg")))
        || !QFileInfo::exists(coverDir.filePath(QStringLiteral("fresh-keep.jpg"))))
        return fail("purge stale covers");
    for (const CoverJob &job : coverJobs(page.shelves, dir.path(), now)) {
        if (job.url == QStringLiteral("https://example.test/dark.jpg"))
            return fail("fresh cover still queued");
    }
    const QVector<PlayerKeyEvent> left = playerKeyEvents(QStringLiteral("left"));
    const QVector<PlayerKeyEvent> okKey = playerKeyEvents(QStringLiteral("ok"));
    const QVector<PlayerKeyEvent> backKey = playerKeyEvents(QStringLiteral("back"));
    if (left.size() != 3 || left.at(0).type != QLatin1String("rawKeyDown") || left.at(1).key != QLatin1String("ArrowLeft")
        || left.at(1).virtualKey != 37)
        return fail("player left key");
    if (okKey.size() != 3 || okKey.at(1).key != QLatin1String(" ") || okKey.at(1).code != QLatin1String("Space")
        || okKey.at(1).virtualKey != 32)
        return fail("player ok key");
    if (backKey.size() != 3 || backKey.at(1).key != QLatin1String("BrowserBack") || backKey.at(1).virtualKey != 166)
        return fail("player back key");
    const VoiceIntent busca = parseVoicePtBr(QStringLiteral("buscar dark na netflix"));
    if (busca.kind != VoiceKind::Search || busca.service != QLatin1String("netflix") || busca.query != QLatin1String("dark"))
        return fail("voice search");
    if (!mirrorPageHasTitles(page) || mirrorPageUsable(page, 4))
        return fail("usable threshold");
    if (netflix->feeds.isEmpty() || !netflix->feeds.at(0).hrefTemplate.contains(QStringLiteral("{href}"))
        || youtube->feeds.isEmpty() || !youtube->feeds.at(0).hrefKeys.contains(QStringLiteral("videoId"))
        || prime->feeds.isEmpty())
        return fail("mirror feeds");
    if (!mirrorDomSnapshotScript().contains(QStringLiteral("links"))
        || !mirrorDomSnapshotScript().contains(QStringLiteral("shadowRoot"))
        || !mirrorDomSnapshotScript().contains(QStringLiteral("JSON.stringify")))
        return fail("dom snapshot");

    const QByteArray netflixBody = QByteArray(R"json({
      "lolomo": {
        "listContext": "continueWatching",
        "title": "Continuar assistindo",
        "items": [
          {"title": "Dark", "watchId": 80100172, "boxart": "https://example.test/dark.jpg"},
          {"title": "Dois", "watchId": "80000002", "boxart": "https://example.test/2.jpg"},
          {"title": "Tres", "watchId": "80000003", "boxart": "https://example.test/3.jpg"},
          {"title": "Quatro", "watchId": "80000004", "boxart": "https://example.test/4.jpg"}
        ]
      }
    })json");
    NetworkSample netflixSample;
    netflixSample.url = QStringLiteral("https://www.netflix.com/nq/website/memberapi/release");
    netflixSample.body = netflixBody;
    const MirrorPage fed = parseMirrorFeeds({netflixSample}, *netflix);
    const ShelfData *fedContinue = nullptr;
    for (const ShelfData &shelf : fed.shelves) {
        if (shelf.id.endsWith(QStringLiteral(":continue")))
            fedContinue = &shelf;
    }
    if (mirrorTitleCount(fed) != 4 || !mirrorPageUsable(fed, 4) || !fedContinue || fedContinue->rows.size() != 4
        || fedContinue->rows.at(0).href != QStringLiteral("https://www.netflix.com/watch/80100172")
        || !fedContinue->rows.at(0).artworkSpec.startsWith(QStringLiteral("https://")))
        return fail("netflix json feed");
    NetworkSample wrongUrl = netflixSample;
    wrongUrl.url = QStringLiteral("https://example.test/other");
    if (mirrorPageHasTitles(parseMirrorFeeds({wrongUrl}, *netflix)))
        return fail("feed url filter");
    NetworkSample thin = netflixSample;
    thin.body = QByteArray(R"json({"items":[{"title":"So um","watchId":"80100172","boxart":"https://example.test/a.jpg"}]})json");
    const MirrorPage thinPage = parseMirrorFeeds({thin}, *netflix);
    if (!mirrorPageHasTitles(thinPage) || mirrorPageUsable(thinPage, 4))
        return fail("thin feed");

    const QByteArray youtubeBody = QByteArray(R"json({
      "contents": {
        "title": {"simpleText": "Continuar assistindo"},
        "items": [
          {"videoId": "abcdefghijk", "title": {"runs": [{"text": "Dark"}]}, "thumbnail": {"thumbnails": [{"url": "https://i.ytimg.com/vi/abcdefghijk/hqdefault.jpg"}]}},
          {"videoId": "bbbbbbbbbbb", "title": {"runs": [{"text": "Dois"}]}, "thumbnail": {"thumbnails": [{"url": "https://i.ytimg.com/vi/b/hqdefault.jpg"}]}},
          {"videoId": "ccccccccccc", "title": {"runs": [{"text": "Tres"}]}, "thumbnail": {"thumbnails": [{"url": "https://i.ytimg.com/vi/c/hqdefault.jpg"}]}},
          {"videoId": "ddddddddddd", "title": {"runs": [{"text": "Quatro"}]}, "thumbnail": {"thumbnails": [{"url": "https://i.ytimg.com/vi/d/hqdefault.jpg"}]}}
        ]
      }
    })json");
    NetworkSample youtubeSample;
    youtubeSample.url = QStringLiteral("https://www.youtube.com/youtubei/v1/browse");
    youtubeSample.body = youtubeBody;
    const MirrorPage youtubePage = parseMirrorFeeds({youtubeSample}, *youtube);
    if (!mirrorPageUsable(youtubePage, 4) || youtubePage.shelves.isEmpty()
        || !youtubePage.shelves.at(0).id.endsWith(QStringLiteral(":continue"))
        || youtubePage.shelves.at(0).rows.at(0).href != QStringLiteral("https://www.youtube.com/watch?v=abcdefghijk"))
        return fail("youtube json feed");
    NetworkSample emptySample;
    emptySample.body = QByteArrayLiteral("{}");
    if (mirrorPageHasTitles(parseMirrorFeeds({emptySample}, *netflix)))
        return fail("empty feed");

    QDir().mkpath(dir.filePath(QStringLiteral("overlay/adapters")));
    QFile overlayFile(dir.filePath(QStringLiteral("overlay/adapters/netflix.json")));
    if (!overlayFile.open(QIODevice::WriteOnly))
        return fail("overlay file");
    overlayFile.write(QByteArray(R"json({
      "id": "netflix",
      "name": "Netflix",
      "kind": "netflix",
      "homeUrl": "https://www.netflix.com/browse",
      "rowSelector": "learned-row"
    })json"));
    overlayFile.close();
    const QVector<MirrorAdapter> overlaidAdapters = loadMirrorAdapters(dir.filePath(QStringLiteral("overlay")));
    const MirrorAdapter *overlaid = findMirrorAdapter(overlaidAdapters, QStringLiteral("netflix"));
    const MirrorAdapter *overlaidYoutube = findMirrorAdapter(overlaidAdapters, QStringLiteral("youtube"));
    if (!overlaid || !overlaidYoutube || overlaid->rowSelector != QLatin1String("learned-row")
        || overlaidYoutube->kind != QLatin1String("youtube") || overlaidAdapters.size() != 7)
        return fail("adapter overlay");

    const QString learnRoot = dir.filePath(QStringLiteral("learn"));
    MirrorAdapter versioned = *netflix;
    versioned.rowSelector = QStringLiteral("v1");
    if (!saveLearnedAdapter(learnRoot, versioned) || versioned.version != 1)
        return fail("save adapter");
    versioned.rowSelector = QStringLiteral("v2");
    if (!saveLearnedAdapter(learnRoot, versioned) || versioned.version != 2)
        return fail("save adapter history");
    QFile historyFile(learnRoot + QStringLiteral("/adapters/history/netflix/1.json"));
    if (!historyFile.open(QIODevice::ReadOnly) || !historyFile.readAll().contains("v1"))
        return fail("adapter history");
    historyFile.close();
    if (!rollbackLearnedAdapter(learnRoot, QStringLiteral("netflix")))
        return fail("rollback adapter");
    QFile rolled(learnRoot + QStringLiteral("/adapters/netflix.json"));
    if (!rolled.open(QIODevice::ReadOnly))
        return fail("rolled file");
    const QByteArray rolledBytes = rolled.readAll();
    rolled.close();
    if (!rolledBytes.contains("v1") || !rolledBytes.contains("\"version\": 1"))
        return fail("rolled content");
    if (!QFile::exists(learnRoot + QStringLiteral("/adapters/history/netflix/1.json"))
        || rollbackLearnedAdapter(learnRoot, QStringLiteral("netflix")))
        return fail("rollback keeps history");

    if (!validateLearnedAdapter(*netflix, {netflixSample}, 4))
        return fail("validate feed");
    MirrorAdapter broken = *netflix;
    broken.feeds[0].hrefKeys = QStringList{QStringLiteral("nope")};
    MirrorAdapter domOnly = *netflix;
    domOnly.feeds.clear();
    if (validateLearnedAdapter(broken, {netflixSample}, 4) || validateLearnedAdapter(domOnly, {netflixSample}, 4)
        || validateLearnedAdapter(*netflix, {}, 4))
        return fail("reject bad adapter");

    QJsonObject feedObj;
    feedObj.insert(QStringLiteral("urlContains"), QJsonArray{QStringLiteral("netflix.com/nq")});
    feedObj.insert(QStringLiteral("titleKeys"), QJsonArray{QStringLiteral("title")});
    feedObj.insert(QStringLiteral("hrefKeys"), QJsonArray{QStringLiteral("watchId")});
    feedObj.insert(QStringLiteral("artworkKeys"), QJsonArray{QStringLiteral("boxart")});
    feedObj.insert(QStringLiteral("shelfKeys"), QJsonArray{QStringLiteral("title")});
    feedObj.insert(QStringLiteral("hrefTemplate"), QStringLiteral("https://www.netflix.com/watch/{href}"));
    QJsonObject learnedObj;
    learnedObj.insert(QStringLiteral("id"), QStringLiteral("netflix"));
    learnedObj.insert(QStringLiteral("rowSelector"), QStringLiteral(".learned"));
    learnedObj.insert(QStringLiteral("feeds"), QJsonArray{feedObj});
    const QByteArray fenced = QByteArrayLiteral("```json\n") + QJsonDocument(learnedObj).toJson(QJsonDocument::Compact)
                              + QByteArrayLiteral("\n```");
    const QString applyRoot = dir.filePath(QStringLiteral("apply"));
    if (!applyLearnResponse(applyRoot, *netflix, {netflixSample}, fenced))
        return fail("apply learn");
    const QVector<MirrorAdapter> savedAdapters = loadMirrorAdapters(applyRoot);
    const MirrorAdapter *saved = findMirrorAdapter(savedAdapters, QStringLiteral("netflix"));
    if (!saved || saved->rowSelector != QLatin1String(".learned") || saved->version != 1 || saved->kind != QLatin1String("netflix"))
        return fail("learned overlay");
    const QByteArray domResponse = QByteArrayLiteral("```json\n{\"id\":\"netflix\",\"rowSelector\":\".sumiu\"}\n```");
    if (applyLearnResponse(dir.filePath(QStringLiteral("reject")), *netflix, {netflixSample}, domResponse))
        return fail("reject dom adapter");
    QJsonObject message;
    message.insert(QStringLiteral("content"), QString::fromUtf8(fenced));
    QJsonObject choice;
    choice.insert(QStringLiteral("message"), message);
    QJsonObject completion;
    completion.insert(QStringLiteral("choices"), QJsonArray{choice});
    if (!applyLearnResponse(dir.filePath(QStringLiteral("chat")), *netflix, {netflixSample},
                            QJsonDocument(completion).toJson(QJsonDocument::Compact)))
        return fail("parse fenced chat");

    qunsetenv("ZAPPE_LLM_URL");
    qunsetenv("ZAPPE_LLM_KEY");
    qunsetenv("ZAPPE_LLM_MODEL");
    qunsetenv("ZAPPE_LLM_FALLBACK_MODEL");
    qunsetenv("ZAPPE_LLM_SESSION_HEADER");
    qunsetenv("ZAPPE_LLM_USER_AGENT");
    QDir().mkpath(dir.filePath(QStringLiteral("llm-off")));
    QFile llmOff(dir.filePath(QStringLiteral("llm-off/llm.json")));
    if (!llmOff.open(QIODevice::WriteOnly) || llmOff.write(QByteArrayLiteral("{\"url\":\"\"}")) < 0)
        return fail("llm off file");
    llmOff.close();
    if (llmEnabled(loadLlmConfig(dir.filePath(QStringLiteral("llm-off")))))
        return fail("llm disabled");
    QDir().mkpath(dir.filePath(QStringLiteral("llm-on")));
    QFile llmOn(dir.filePath(QStringLiteral("llm-on/llm.json")));
    if (!llmOn.open(QIODevice::WriteOnly)
        || llmOn.write(QByteArrayLiteral("{\"url\":\"https://llm.example/v1/chat/completions\",\"model\":\"local\"}")) < 0)
        return fail("llm on file");
    llmOn.close();
    const LlmConfig local = loadLlmConfig(dir.filePath(QStringLiteral("llm-on")));
    if (!llmEnabled(local) || local.model != QLatin1String("local"))
        return fail("llm enabled");
    qputenv("ZAPPE_LLM_URL", "https://env.example/v1/chat/completions");
    if (loadLlmConfig(dir.filePath(QStringLiteral("llm-on"))).url != QLatin1String("https://env.example/v1/chat/completions"))
        return fail("llm env");
    qunsetenv("ZAPPE_LLM_URL");
    QDir().mkpath(dir.filePath(QStringLiteral("llm-go")));
    QFile llmGo(dir.filePath(QStringLiteral("llm-go/llm.json")));
    const QByteArray goJson = QByteArrayLiteral(
        "{\"url\":\"https://go.example/v1/chat/completions\",\"key\":\"sk-go-secret\","
        "\"model\":\"go-main\",\"fallbackModel\":\"go-fallback\","
        "\"sessionHeader\":\"x-opencode-session\",\"userAgent\":\"zappe-tv/9\"}");
    if (!llmGo.open(QIODevice::WriteOnly) || llmGo.write(goJson) != goJson.size())
        return fail("llm go file");
    llmGo.close();
    const LlmConfig go = loadLlmConfig(dir.filePath(QStringLiteral("llm-go")));
    const QDate goDay(2026, 10, 18);
    if (go.url != QLatin1String("https://go.example/v1/chat/completions") || go.key != QLatin1String("sk-go-secret")
        || go.model != QLatin1String("go-main") || go.fallbackModel != QLatin1String("go-fallback")
        || go.sessionHeader != QLatin1String("x-opencode-session") || go.userAgent != QLatin1String("zappe-tv/9")
        || llmSessionId(QStringLiteral("Globoplay"), goDay) != QLatin1String("zappe-globoplay-20261018")
        || llmSessionId(QStringLiteral("netflix"), goDay) == llmSessionId(QStringLiteral("prime"), goDay))
        return fail("llm go config");
    bool sawSession = false;
    bool sawAgent = false;
    bool sawAuth = false;
    bool leaked = false;
    for (const LlmHeader &header : llmRequestHeaders(go, QStringLiteral("netflix"), goDay)) {
        if (header.name == "User-Agent" && header.value == "zappe-tv/9")
            sawAgent = true;
        if (header.name == "x-opencode-session" && header.value == "zappe-netflix-20261018")
            sawSession = true;
        if (header.name == "Authorization" && header.value == "Bearer sk-go-secret")
            sawAuth = true;
        if (header.name != "Authorization" && QString::fromUtf8(header.value).contains(QStringLiteral("sk-go-secret")))
            leaked = true;
    }
    LlmConfig plain;
    bool plainSession = false;
    QByteArray plainAgent;
    for (const LlmHeader &header : llmRequestHeaders(plain, QStringLiteral("netflix"), goDay)) {
        if (header.name == "User-Agent")
            plainAgent = header.value;
        if (header.name == "x-opencode-session" || header.value.startsWith("zappe-"))
            plainSession = true;
    }
    if (!sawSession || !sawAgent || !sawAuth || leaked || plainSession || plainAgent != "zappe/0.2.0")
        return fail("llm headers");
    if (!llmUseFallback(true, false, false, QStringLiteral("go-fallback"), QStringLiteral("go-main"))
        || !llmUseFallback(false, true, false, QStringLiteral("go-fallback"), QStringLiteral("go-main"))
        || llmUseFallback(true, true, true, QStringLiteral("go-fallback"), QStringLiteral("go-main"))
        || llmUseFallback(true, true, false, QString(), QStringLiteral("go-main"))
        || llmUseFallback(false, false, false, QStringLiteral("go-fallback"), QStringLiteral("go-main"))
        || llmUseFallback(true, false, false, QStringLiteral("go-main"), QStringLiteral("go-main")))
        return fail("llm fallback");
    const QString goLog = llmResultLog(QStringLiteral("netflix"), QStringLiteral("go-main"), 400, 90);
    if (!goLog.contains(QStringLiteral("model go-main")) || !goLog.contains(QStringLiteral("http 400"))
        || !goLog.contains(QStringLiteral("90 ms")) || goLog.contains(QStringLiteral("sk-go-secret")))
        return fail("llm log");
    qputenv("ZAPPE_LLM_FALLBACK_MODEL", "from-env");
    if (loadLlmConfig(dir.filePath(QStringLiteral("llm-go"))).fallbackModel != QLatin1String("from-env"))
        return fail("llm fallback env");
    qunsetenv("ZAPPE_LLM_FALLBACK_MODEL");

    LlmConfig secret;
    secret.key = QStringLiteral("sk-live-DO-NOT-LEAK");
    secret.model = QStringLiteral("local-model");
    const QString domTail = QString(20000, QLatin1Char('x')) + QStringLiteral("https://globoplay.globo.com/novela/t/12345");
    QByteArray lateBody(30000, ' ');
    lateBody += QByteArray(R"json({"title":"Novela das nove","href":"/novela/t/12345","image":"https://img.example/novela.jpg"})json");
    const QString lateDigest = jsonLearnDigest(lateBody, 8);
    if (!lateDigest.contains(QStringLiteral("Novela das nove")) || !lateDigest.contains(QStringLiteral("/novela/t/12345"))
        || !lateDigest.contains(QStringLiteral("[title]")) || !lateDigest.contains(QStringLiteral("[href]"))
        || !lateDigest.contains(QStringLiteral("https://img.example/novela.jpg")))
        return fail("json digest keeps the tail");
    NetworkSample lateSample;
    lateSample.url = QStringLiteral("https://multicloud-jarvis.globo.com/graphql");
    lateSample.body = lateBody;
    const QByteArray repair = llmRepairBody(*netflix, domTail, {netflixSample, lateSample}, QByteArrayLiteral("abc"),
                                            secret.model);
    if (!QJsonDocument::fromJson(repair).isObject() || repair.contains(secret.key.toUtf8()) || !repair.contains("local-model")
        || !repair.contains("Dark") || !repair.contains("hrefTemplate") || !repair.contains("data:image/jpeg;base64")
        || !repair.contains("https://globoplay.globo.com/novela/t/12345")
        || !repair.contains("Novela das nove") || !repair.contains("multicloud-jarvis.globo.com/graphql"))
        return fail("llm body");
    const QString accepted = learnValidationReport(*netflix, {netflixSample}, fenced, 4);
    const QString rejected = learnValidationReport(*netflix, {netflixSample}, domResponse, 4);
    if (!accepted.contains(QStringLiteral("títulos=4")) || !accepted.contains(QStringLiteral("com capa http=4"))
        || !accepted.contains(QStringLiteral("aceito")) || !accepted.contains(QStringLiteral("Dark"))
        || !accepted.contains(QStringLiteral("titleKeys=title")) || accepted.contains(QStringLiteral("sk-"))
        || !rejected.contains(QStringLiteral("rejeitado")) || !rejected.contains(QStringLiteral("sem feeds"))
        || !rejected.contains(QStringLiteral("títulos=0")) || rejected.size() < 40)
        return fail("validation report");
    const QByteArray huge = llmRepairBody(*netflix, QStringLiteral("dom"), {netflixSample}, QByteArray(80001, 'x'), secret.model);
    if (huge.contains("data:image") || !llmAuthorization(secret).contains(secret.key.toUtf8()) || !llmAuthorization(LlmConfig()).isEmpty())
        return fail("llm secret");

    std::printf("logic-check: ok (map, 7 streamings, ota, voice, hdmi, samsung, chrome)\n");
    return 0;
}

} // namespace zappe
