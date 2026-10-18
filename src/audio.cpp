#include "audio.h"

#include <QProcess>

#include <cstdio>

namespace zappe {

HdmiAudio defaultHdmiAudio()
{
    return {};
}

HdmiAudio hdmiAudioFromEnv()
{
    HdmiAudio audio = defaultHdmiAudio();
    const QByteArray card = qgetenv("ZAPPE_HDMI_CARD");
    const QByteArray profile = qgetenv("ZAPPE_HDMI_PROFILE");
    if (!card.isEmpty())
        audio.card = QString::fromUtf8(card);
    if (!profile.isEmpty())
        audio.profile = QString::fromUtf8(profile);
    return audio;
}

QStringList pactlProfileArgs(const HdmiAudio &audio)
{
    return {QStringLiteral("set-card-profile"), audio.card, audio.profile};
}

void applyHdmiAudio()
{
    const QByteArray disabled = qgetenv("ZAPPE_AUDIO_DISABLE");
    if (disabled == "1" || disabled == "true" || disabled == "yes" || disabled == "on")
        return;
    const HdmiAudio audio = hdmiAudioFromEnv();
    QProcess pactl;
    pactl.start(QStringLiteral("pactl"), pactlProfileArgs(audio));
    if (!pactl.waitForStarted(1500)) {
        std::fprintf(stderr, "zappe: pactl não iniciou; perfil HDMI não aplicado\n");
        return;
    }
    pactl.waitForFinished(4000);
    if (pactl.exitStatus() != QProcess::NormalExit || pactl.exitCode() != 0) {
        std::fprintf(stderr, "zappe: pactl set-card-profile %s %s falhou\n", qPrintable(audio.card),
                     qPrintable(audio.profile));
    } else {
        std::fprintf(stderr, "zappe: áudio %s %s\n", qPrintable(audio.card), qPrintable(audio.profile));
    }
}

} // namespace zappe
