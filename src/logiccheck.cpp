#include "logiccheck.h"

#include "audio.h"
#include "cdp.h"
#include "chrome.h"
#include "display.h"
#include "keymap.h"
#include "logins.h"
#include "ota.h"
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
    if (actionForEvdev(116) != RemoteAction::Ignore)
        return fail("power 116 must be ignored");
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
    const QString ws = samsungWebsocketUrl(samsung, false);
    if (!ws.startsWith(QStringLiteral("ws://192.168.3.6:8001/")) || !ws.contains(QStringLiteral("name=")))
        return fail("samsung url");

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

    std::printf("logic-check: ok (map, 7 streamings, ota, voice, hdmi, chrome)\n");
    return 0;
}

} // namespace zappe
