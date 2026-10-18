#pragma once

#include <QString>
#include <QStringList>

namespace zappe {

// Saída forçada no HDMI da iGPU do mini-PC (PipeWire / Pulse).
struct HdmiAudio {
    QString card = QStringLiteral("alsa_card.pci-0000_00_1f.3");
    QString profile = QStringLiteral("output:hdmi-stereo");
};

HdmiAudio defaultHdmiAudio();
HdmiAudio hdmiAudioFromEnv();
QStringList pactlProfileArgs(const HdmiAudio &audio);

// Best-effort. Falha de pactl não derruba o app.
void applyHdmiAudio();

} // namespace zappe
