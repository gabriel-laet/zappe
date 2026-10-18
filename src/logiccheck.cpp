#include "logiccheck.h"

#include "audio.h"
#include "cdp.h"
#include "chrome.h"
#include "display.h"
#include "keymap.h"
#include "logins.h"
#include "ota.h"
#include "remote.h"
#include "samsung.h"
#include "voice.h"

#include <QDir>
#include <QFile>
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
        || actionForEvdev(14) != RemoteAction::Back)
        return fail("ok/home/del");
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
    if (!script.contains(QStringLiteral("dvbv5-zap")) || !script.contains(QStringLiteral("mpv")))
        return fail("ota pipeline");

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
    if (chrome.port != 9222 && qEnvironmentVariable("ZAPPE_CDP_PORT").isEmpty())
        return fail("cdp port");
    if (!joined.contains(QStringLiteral("--kiosk")) || !joined.contains(QStringLiteral("--remote-debugging-port="))
        || !joined.contains(QStringLiteral("chrome-profile")) || joined.contains(QStringLiteral("gamescope")))
        return fail("chrome args");
    if (!cdpCommand(7, QStringLiteral("Page.navigate"), QJsonObject{{QStringLiteral("url"), QStringLiteral("https://example")}})
             .contains("Page.navigate"))
        return fail("cdp command");
    if (!fillScript(QStringLiteral("input[type='email']"), QStringLiteral("a@b.c")).contains(QStringLiteral("querySelector")))
        return fail("fill script");

    std::printf("logic-check: ok (map, 7 streamings, ota, voice, hdmi, samsung, chrome)\n");
    return 0;
}

} // namespace zappe
