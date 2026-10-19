#include "logiccheck.h"

#include "audio.h"
#include "catalog.h"
#include "cdp.h"
#include "chrome.h"
#include "display.h"
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

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QBuffer>
#include <QProcess>
#include <QProcessEnvironment>
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
    if (whisperAudioCtx(0) != 256 || whisperAudioCtx(1000) != 256 || whisperAudioCtx(-5) != 256)
        return fail("audio ctx short");
    if (whisperAudioCtx(5000) != 314)
        return fail("audio ctx 5s");
    if (whisperAudioCtx(20000) != 1064)
        return fail("audio ctx 20s");
    if (whisperAudioCtx(28720) != 1500 || whisperAudioCtx(40000) != 1500)
        return fail("audio ctx cap");
    {
        constexpr int rate = 16000;
        const int lead = rate / 2;
        const int speech = rate / 5;
        const int tail = rate;
        QVector<qint16> pcm(lead + speech + tail, 0);
        for (int i = 0; i < speech; ++i)
            pcm[lead + i] = (i % 2) ? 8000 : -8000;
        const SilenceTrim trim = trimSilencePcm(pcm.constData(), pcm.size(), rate);
        const int end = trim.startSample + trim.sampleCount;
        const int pad = rate / 10;
        if (trim.startSample < lead - pad || trim.startSample > lead)
            return fail("vad start");
        if (end < lead + speech || end > lead + speech + pad)
            return fail("vad end");
        QVector<qint16> quiet(rate, 0);
        const SilenceTrim all = trimSilencePcm(quiet.constData(), quiet.size(), rate);
        if (all.startSample != 0 || all.sampleCount != quiet.size())
            return fail("vad silence");
    }
    if (voiceEditDistance(QStringLiteral("almento"), QStringLiteral("aumenta")) != 2)
        return fail("edit almento");
    if (fuzzyCorrectVoice(QStringLiteral("Almento")) != QLatin1String("aumenta")
        || fuzzyCorrectVoice(QStringLiteral("almento")) != QLatin1String("aumenta"))
        return fail("fuzzy almento");
    if (fuzzyCorrectVoice(fuzzyCorrectVoice(QStringLiteral("Almento"))) != QLatin1String("aumenta"))
        return fail("fuzzy idempotent");
    const VoiceIntent almento = parseVoicePtBr(QStringLiteral("Almento o volume"));
    if (almento.kind != VoiceKind::Volume || almento.delta != 1)
        return fail("fuzzy volume");
    if (fuzzyCorrectVoice(QStringLiteral("mudo sbt hbo max som vai ir")) != QLatin1String("mudo sbt hbo max som vai ir"))
        return fail("fuzzy short");
    if (fuzzyCorrectVoice(QStringLiteral("buscar dark na netflix")) != QLatin1String("buscar dark na netflix"))
        return fail("fuzzy dark");
    const auto flag = [](const QStringList &args, const char *name) {
        const int index = args.indexOf(QLatin1String(name));
        if (index < 0 || index + 1 >= args.size())
            return QString();
        return args.at(index + 1);
    };
    const QStringList cli = whisperCliArgs(QStringLiteral("m.bin"), QStringLiteral("a.wav"), 314);
    if (flag(cli, "-ac") != QLatin1String("314") || flag(cli, "-bs") != QLatin1String("1")
        || flag(cli, "-bo") != QLatin1String("1") || flag(cli, "-t") != QLatin1String("4")
        || flag(cli, "-l") != QLatin1String("pt") || !cli.contains(QStringLiteral("-nt"))
        || !cli.contains(QStringLiteral("--prompt")))
        return fail("cli flags");
    const QStringList serverArgs = whisperServerArgs(QStringLiteral("m.bin"), 8178);
    if (!serverArgs.contains(QStringLiteral("--convert")) || serverArgs.contains(QStringLiteral("-ng"))
        || flag(serverArgs, "-l") != QLatin1String("pt") || !serverArgs.contains(QStringLiteral("-nt"))
        || flag(serverArgs, "-t") != QLatin1String("4") || flag(serverArgs, "-bs") != QLatin1String("1")
        || flag(serverArgs, "-bo") != QLatin1String("1") || flag(serverArgs, "--host") != QLatin1String("127.0.0.1")
        || flag(serverArgs, "--port") != QLatin1String("8178"))
        return fail("server flags");
    QProcessEnvironment gamescope;
    gamescope.insert(QStringLiteral("ENABLE_GAMESCOPE_WSI"), QStringLiteral("1"));
    gamescope.insert(QStringLiteral("HOME"), QStringLiteral("/tmp/zappe"));
    const QProcessEnvironment whisperEnv = whisperChildEnvironment(gamescope);
    if (whisperEnv.value(QStringLiteral("ENABLE_GAMESCOPE_WSI")) != QLatin1String("0")
        || whisperEnv.value(QStringLiteral("HOME")) != QLatin1String("/tmp/zappe"))
        return fail("whisper gamescope wsi");
    const QByteArray wav = wavFromPcm(QByteArray(8, '\x01'), 16000);
    if (wav.size() != 52 || !wav.startsWith("RIFF") || wav.mid(8, 4) != QByteArray("WAVE"))
        return fail("wav header");

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
        || !netflixScript.contains(QStringLiteral("backgroundImage")) || !netflixScript.contains(QStringLiteral("synopsis")))
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
        || coverUrlsMatch(QStringLiteral("https://cdn.example/a/box.jpg"), QStringLiteral("https://cdn.example/b/other.jpg")))
        return fail("cover url match");
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
    if (hboArt.contains(QLatin1Char('{')) || !hboArt.contains(QStringLiteral("w=640"))
        || !hboArt.contains(QStringLiteral("h=360"))
        || appleArt != QStringLiteral("https://is1-ssl.mzstatic.com/image/thumb/Features/v4/xx/source/640x360bb.jpg")
        || encodedArt.contains(QLatin1Char('{')) || !encodedArt.contains(QStringLiteral("w=640")))
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
    if (!mirrorCoverClipScript({QStringLiteral("https://cdn.example/a/box.jpg")}).contains(QStringLiteral("getBoundingClientRect")))
        return fail("cover clip script");
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
    if (mirrorChromeInFront(false) || !mirrorChromeInFront(true) || mirrorRefreshMs() != 20 * 60 * 1000)
        return fail("mirror chrome policy");
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
    if (!mirrorDomSnapshotScript().contains(QStringLiteral("nodes"))
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

    LlmConfig secret;
    secret.key = QStringLiteral("sk-live-DO-NOT-LEAK");
    secret.model = QStringLiteral("local-model");
    const QByteArray repair = llmRepairBody(*netflix, QStringLiteral("{\"nodes\":[]}"), {netflixSample}, QByteArrayLiteral("abc"),
                                            secret.model);
    if (!QJsonDocument::fromJson(repair).isObject() || repair.contains(secret.key.toUtf8()) || !repair.contains("local-model")
        || !repair.contains("Dark") || !repair.contains("hrefTemplate") || !repair.contains("data:image/jpeg;base64"))
        return fail("llm body");
    const QByteArray huge = llmRepairBody(*netflix, QStringLiteral("dom"), {netflixSample}, QByteArray(80001, 'x'), secret.model);
    if (huge.contains("data:image") || !llmAuthorization(secret).contains(secret.key.toUtf8()) || !llmAuthorization(LlmConfig()).isEmpty())
        return fail("llm secret");

    std::printf("logic-check: ok (map, 7 streamings, ota, voice, hdmi, samsung, chrome)\n");
    return 0;
}

} // namespace zappe
