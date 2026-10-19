#pragma once

#include "catalog.h"

#include <QDateTime>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QVector>

class QLocalSocket;
class QTimer;

namespace zappe {

struct OtaChannel {
    QString name;
    QString source;
    QString number;
    bool oneSeg = false;
};

// Um serviço do channels.conf, com a camada ISDB e o número virtual quando existem.
struct OtaService {
    QString name;
    bool oneSeg = false;
    QString number;
    int layer = -1;
};

// O que fazer com a capa do canal em foco. Screenshot não troca a sintonia.
enum class FrameRefresh { Screenshot, Preview, Busy };

QStringList parseChannelNames(const QString &text);
QVector<OtaService> parseOtaServices(const QString &text);
QStringList filterAndSortChannels(QStringList names);
QStringList playableNames(const QVector<OtaService> &services);
QString fullsegGapReason(const QVector<OtaService> &services, const QString &networkId);
// São Paulo: Band virtual 13.1, físico 23 UHF, 527,143 MHz. Não é o físico 13.
int bandPhysicalChannel();
int bandFrequencyHz();
QString bandScanSeed();
// UHF 14–51. A faixa inteira quando o físico 23 não devolve serviço.
QString bandFullScanSeed();
QStringList bandScanArguments(const QString &seedPath, const QString &outPath);
// Texto do toast quando 13 ou 13.1 não está na varredura. Vazio nos outros números.
QString bandRescanPrompt(const QString &typed);
QString bandFullRescanPrompt();
// Acrescenta seções novas sem repetir o nome. O texto existente fica no começo.
QString mergeChannelConfig(const QString &existing, const QString &scanned);
QString primaryChannelConfigPath();
QVector<OtaChannel> otaChannelsFromConf(const QString &text);
QString resolveChannelName(const QString &requested, const QStringList &names);
QString resolveDialTarget(const QString &typed, const QVector<OtaChannel> &channels);
QString appendDialText(const QString &current, const QString &glyph);
int dialCommitMs();
int otaFocusRefreshMs();
QString otaPipelineScript(const QString &conf, const QString &channel);
QString otaPreviewScript(const QString &conf, const QString &channel, const QString &tsPath, const QString &jpgPath);
FrameRefresh planFrameRefresh(bool playing, const QString &playingChannel, const QString &focused);
bool mpvPlaybackStarted(const QByteArray &ipcLine);

QVector<OtaChannel> listOtaChannels();
void mergeLiveChannels(CatalogFile &catalog, const QVector<OtaChannel> &channels);
QString otaFramePath(const QString &dataDir, const QString &channel);
bool otaFrameIsFresh(const QString &path, const QDateTime &now);
int otaFrameIntervalMs();
void assignOtaFrame(RowData &row, const QString &dataDir, const QDateTime &now);

class OtaPlayer : public QObject {
    Q_OBJECT
public:
    explicit OtaPlayer(QObject *parent = nullptr);
    bool playing() const { return m_process != nullptr; }
    QString playingChannel() const { return m_channel; }
    void play(const QString &requested);
    void stop();
    void togglePause();
    // Capa do canal em foco. Se esse canal já está no ar, só tira um frame.
    // Se outro está no ar, não troca. Se o sintonizador está livre, grava um TS curto.
    void refreshFocused(const QString &requested);
    // Solta a prévia. Falso se um canal já está no ar: a busca não toma o sintonizador.
    bool releaseTuner();

signals:
    void started(const QString &channel);
    void failed(const QString &message);
    void stopped();
    void frameReady(const QString &channel, const QString &path);
    void epgReady(const QString &channel, const QString &nowTitle, const QString &nextTitle);

private:
    void pollPicture();
    void armFrames();
    void captureFrame();
    void stopPreview();

    QProcess *m_process = nullptr;
    QProcess *m_preview = nullptr;
    QTimer *m_pictureTimer = nullptr;
    QTimer *m_frameTimer = nullptr;
    QLocalSocket *m_ipc = nullptr;
    QString m_channel;
    QString m_previewChannel;
    QString m_socket;
    bool m_userStop = false;
    bool m_sawPicture = false;
};

} // namespace zappe
