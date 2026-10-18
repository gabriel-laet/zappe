#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QProcess;
class QTimer;

namespace zappe {

enum class VoiceKind { None, Home, Back, PlayPause, Open, Search, Ota, Volume, Mute };

struct VoiceIntent {
    VoiceKind kind = VoiceKind::None;
    QString service;
    QString query;
    int delta = 0;

    bool ok() const { return kind != VoiceKind::None; }
};

// Trecho de PCM que sobra depois de cortar o silêncio das pontas.
struct SilenceTrim {
    int startSample = 0;
    int sampleCount = 0;
};

QString foldVoice(const QString &text);
VoiceIntent parseVoicePtBr(const QString &text);

// Palavras dos comandos conhecidos. "Almento" vira "aumenta".
QStringList voiceLexicon();
int voiceEditDistance(const QString &left, const QString &right);
QString fuzzyCorrectVoice(const QString &text);

// N = clip_ms/20 + 64, preso em 256..1500. É o -ac do whisper.
int whisperAudioCtx(int clipMs);
int whisperPartialIntervalMs();
int whisperServerPort();

QStringList whisperCliArgs(const QString &model, const QString &wav, int audioCtx);
QStringList whisperServerArgs(const QString &model, int port);

// PCM s16le mono. Sem fala, devolve o clipe inteiro.
SilenceTrim trimSilencePcm(const qint16 *samples, int count, int sampleRate);
QByteArray wavFromPcm(const QByteArray &pcm, int sampleRate);
QString whisperTranscriptFrom(const QByteArray &stdoutBytes, const QByteArray &stderrBytes);

// Onde ficam o binário Vulkan e o ggml-small-q8_0.
struct WhisperLayout {
    QString root;
    QString binDir;
    QString model;
    QString serverBin;
    QString cliBin;
    bool serverDisabled = false;
};

WhisperLayout whisperLayout(const QString &dataDir);

// Segura o botão do microfone: parciais a cada 1 s e uma passagem final ao soltar.
// O whisper-server fica residente com o modelo carregado. Sem ele, whisper-cli.
class VoiceSession : public QObject {
    Q_OBJECT
public:
    explicit VoiceSession(QObject *parent = nullptr);
    ~VoiceSession() override;

    void warm(const QString &dataDir);
    void press(const QString &dataDir);
    void release();
    void cancel();

signals:
    void started();
    void partial(const QString &transcript);
    void finished(const QString &transcript);
    void failed(const QString &message);

private:
    void ensureServer();
    void pollHealth();
    void startRecord();
    void stopRecord();
    void onPartialTick();
    void transcribeFile(bool finalPass);
    void spawnServer();
    void kickFinal();
    void finishRequest(bool finalPass, int generation, int requestId, qint64 startedMs, int audioCtx,
                       const QByteArray &body, const QByteArray &err, bool ok, bool fromCli);
    void runCli(const QString &wav, bool finalPass, int generation, int requestId, int audioCtx, qint64 startedMs);

    bool m_holding = false;
    bool m_busy = false;
    bool m_inflight = false;
    bool m_finalPending = false;
    bool m_serverReady = false;
    bool m_serverStarting = false;
    bool m_fake = false;
    bool m_stopRequested = false;
    bool m_spawned = false;
    bool m_healthBusy = false;
    int m_generation = 0;
    int m_requestId = 0;
    int m_healthTries = 0;
    qint64 m_holdStart = 0;
    QString m_dataDir;
    QString m_fakeText;
    QString m_rawPath;
    QString m_wavPath;
    QProcess *m_record = nullptr;
    QProcess *m_server = nullptr;
    QTimer *m_partialTimer = nullptr;
    QTimer *m_healthTimer = nullptr;
    QNetworkAccessManager *m_net = nullptr;
};

} // namespace zappe
