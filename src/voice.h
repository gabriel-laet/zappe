#pragma once

#include <QObject>
#include <QString>

namespace zappe {

enum class VoiceKind { None, Home, Back, PlayPause, Open, Ota, Volume, Mute };

struct VoiceIntent {
    VoiceKind kind = VoiceKind::None;
    QString service;
    QString query;
    int delta = 0;

    bool ok() const { return kind != VoiceKind::None; }
};

QString foldVoice(const QString &text);
VoiceIntent parseVoicePtBr(const QString &text);

// Grava o microfone e transcreve com whisper.cpp em pt. Modelo: base, depois tiny.
class VoiceSession : public QObject {
    Q_OBJECT
public:
    explicit VoiceSession(QObject *parent = nullptr);

    void listen(const QString &dataDir);
    void cancel();

signals:
    void started();
    void finished(const QString &transcript);
    void failed(const QString &message);

private:
    void transcribe(const QString &wav);
    bool m_busy = false;
};

QString whisperModel(const QString &dataDir);

} // namespace zappe
