#pragma once

#include "catalog.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

class QNetworkAccessManager;
class QTimer;

namespace zappe {

class ChromeSession;
class CdpClient;
class Guide;

struct MirrorFeed {
    QStringList urlContains;
    QStringList titleKeys;
    QStringList hrefKeys;
    QStringList artworkKeys;
    QStringList shelfKeys;
    QString hrefTemplate;
};

struct MirrorAdapter {
    QString id;
    QString name;
    QString kind;
    QString homeUrl;
    QString searchUrl;
    QString rowSelector;
    QString rowTitleSelector;
    QString cardSelector;
    QString linkSelector;
    QString imageSelector;
    QString progressSelector;
    QString profileSelector;
    QStringList continueHints;
    QStringList extraUrls;
    QStringList rejectHints;
    QStringList playPaths;
    QString playClick;
    QString playerOk;
    QString playerSeek;
    QVector<MirrorFeed> feeds;
    bool playerClick = false;
    int limitRows = 12;
    int limitCards = 18;
    int version = 0;
};

struct NetworkSample {
    QString url;
    QByteArray body;
};

struct MirrorPage {
    QVector<ShelfData> shelves;
    QStringList profiles;
    bool needsLogin = false;
    bool ok = false;
};

bool mirrorReadAdapter(const QJsonObject &obj, MirrorAdapter &adapter);
QByteArray mirrorAdapterJson(const MirrorAdapter &adapter);
QVector<MirrorAdapter> loadMirrorAdapters();
QVector<MirrorAdapter> loadMirrorAdapters(const QString &dataDir);
QVector<MirrorAdapter> parseMirrorAdapters(const QByteArray &json);
const MirrorAdapter *findMirrorAdapter(const QVector<MirrorAdapter> &adapters, const QString &id);

QString mirrorExtractScript(const MirrorAdapter &adapter);
QString mirrorDomSnapshotScript();
QString mirrorFocusPlayerScript(const MirrorAdapter &adapter);
QString mirrorTransportScript(const QString &action);
QString mirrorSearchUrl(const MirrorAdapter &adapter, const QString &query);
QString mirrorPlayUrl(const MirrorAdapter &adapter, const QString &href);
bool mirrorIsPlayable(const MirrorAdapter &adapter, const QString &href);
// Home, busca e refresh do catálogo não podem abrir /watch nem watch?v=.
bool mirrorUrlIsPlayback(const QString &url);
// OK numa fileira local do serviço abre o espelho, em vez da ficha morta.
QString mirrorRowService(const QString &service, const QString &href);
// Leitura nova muito menor que a anterior: fica a página mais rica.
bool mirrorKeepRicherPage(int previousTitles, int incomingTitles);
// Ainda não há fileira: a tela segue em «Atualizando…», sem «Nada nesta página».
bool mirrorHoldLoading(int previousTitles, int incomingTitles);
// Hero de canal ao vivo não entra na fileira do guia. O href sozinho não descarta.
bool mirrorLiveHero(const QString &title, const QString &href);
// «LIVE» / «Ao vivo» é selo do card, não o nome da fileira nem do título.
bool mirrorBadgeTitle(const QString &title);
// Miniatura pública do YouTube quando o DOM não trouxe URL de imagem.
QString mirrorYoutubeArtwork(const QString &href);
// 401/403: o CDN recusou o GET sem o cookie da aba. A página busca de novo.
bool coverHttpNeedsPage(int status);
// GIF 1×1 e pixel de rastreio não são capa.
bool coverUrlIsPlaceholder(const QString &url);
// YouTube não tem perfis. "Account menu" também não é um perfil.
bool mirrorKeepsProfiles(const MirrorAdapter &adapter);
bool mirrorProfileLabel(const QString &name);
// Casa a capa no card pela URL sem query. changed fica true se algum path mudou.
int assignCoverFile(QVector<ShelfData> &shelves, const QString &url, const QString &path, bool *changed = nullptr);
// Apaga covers/*.img e JPEG sem uso há mais de 14 dias.
int purgeStaleCovers(const QString &cacheDir, const QDateTime &now);
QString mirrorCoverFetchScript(const QStringList &urls);
QString mirrorCoverClipScript(const QStringList &urls, const QStringList &titles = {});
// Host + path, sem query. CDN assinado muda o token e mantém o arquivo.
QString coverMatchKey(const QString &url);
bool coverUrlsMatch(const QString &wanted, const QString &loaded);
// Trecho longo e único do path. Casa a imagem que o Chrome baixou quando o GET direto erra.
QString coverLooseToken(const QString &url);
// Referer do serviço (https://www.netflix.com/...), não o host do CDN.
QString coverDownloadReferer(const QString &homeUrl);
// Troca {width}, {w}x{h}{c}.{f} e afins por um tamanho real. Idempotente.
QString expandCoverUrl(const QString &url);
// JPEG pronto para o card. detail descreve o corpo quando a conversão falha.
// mime (Content-Type) acompanha o GET direto. AVIF cai no ffmpeg por arquivo
// temporário: o pipe não busca o átomo e -f avif não é demuxer.
QByteArray coverJpegFromBody(const QByteArray &body, QString *detail, const QString &mime = {});
QString coverBodyLabel(const QByteArray &body);
// TypeError de CORS/rede: repetir o fetch não traz a imagem.
bool coverFetchIsBlocked(const QString &error);

MirrorPage parseMirrorPage(const QByteArray &json, const MirrorAdapter &adapter);
MirrorPage parseMirrorFeeds(const QVector<NetworkSample> &samples, const MirrorAdapter &adapter);
bool mirrorPageHasTitles(const MirrorPage &page);
int mirrorTitleCount(const MirrorPage &page);
bool mirrorPageUsable(const MirrorPage &page, int minTitles);

QString coverCachePath(const QString &cacheDir, const QString &url);
bool coverIsFresh(const QString &path, const QDateTime &now, int maxAgeSecs);
int mirrorCoverMaxAgeSecs();
int mirrorCoverUnusedMaxAgeSecs();
int mirrorRefreshMs();
int mirrorLoadTimeoutMs();
// Fileira de erro do espelho: OK dispara de novo em vez de abrir o detalhe.
bool mirrorShelfRetries(const QString &status, const QString &skillId, QString &serviceId);

// Mantém o Qt na frente. O Chrome só aparece quando o player está aberto.
bool mirrorChromeInFront(bool playerOpen);

struct CoverJob {
    QString serviceId;
    QString url;
    QString path;
    QString referer;
    QString title;
};

struct HeldImage {
    QString url;
    QString key;
    QString file;
    QByteArray body;
};

struct CoverShot {
    CoverJob job;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

QVector<CoverJob> coverJobs(const QVector<ShelfData> &shelves, const QString &cacheDir, const QDateTime &now);

class MirrorSession : public QObject {
    Q_OBJECT
public:
    explicit MirrorSession(QObject *parent = nullptr);

    void start(Guide *guide, ChromeSession *chrome, const QString &dataDir);
    void refresh(const QString &serviceId);
    void search(const QString &serviceId, const QString &query);
    void chooseProfile(const QString &serviceId, const QString &name);
    void play(const QString &serviceId, const QString &href);
    void leavePlayer();
    void dispatchAction(const QString &action);
    void focusPlayer();
    bool playerOpen() const { return m_player; }
    bool active() const { return m_started; }

signals:
    void playerReady();
    void needsLogin(const QString &serviceId);
    void failed(const QString &message);

private:
    void onChromeReady();
    void onAttached();
    void onNavigated();
    void onEvaluated(const QString &value, bool ok);
    void onNetworkResponse(const QString &requestId, const QString &url, const QString &mimeType);
    void onNetworkFinished(const QString &requestId);
    void onResponseBody(const QString &requestId, const QByteArray &body, bool ok);
    void onScreenshot(const QByteArray &png, bool ok);
    void pump();
    void begin(const QString &serviceId, const QString &url, int job);
    void settle(int epoch, const QString &serviceId, int job, int waited);
    void considerExtract();
    void acceptPage(const MirrorPage &page);
    void beginRepair();
    void postLearn(const QByteArray &png);
    void postLearnRequest(const QString &serviceId, const QByteArray &png, const QString &model, bool triedFallback);
    void finishEmpty();
    bool applyPage(const MirrorPage &page);
    void downloadNext();
    void noteArtwork(const MirrorPage &page);
    int takeHeldCovers();
    QByteArray heldImage(const QString &url) const;
    void rememberImage(const QString &url, const QByteArray &body);
    bool fetchNextCoverBatch();
    bool startCoverClips();
    void shootNextClip();
    void coverTimedOut(int gen);
    void finishCoverPass();
    bool startDeniedFetch();
    void noteDeniedCover(const CoverJob &job);
    void logCoverSummary();
    void dropCurrentCoverJobs();
    void startDirectCovers(const QVector<CoverJob> &jobs);
    void pumpDirectCovers();
    void logDirectCovers();
    void finishAccept();
    int storeCover(const CoverJob &job, const QByteArray &body);
    int saveFromNetwork(const CoverJob &job);
    void attachHeldCover(const QString &url, const QByteArray &body);
    void onFrame(const QString &url);
    void showLoading(const QString &serviceId);
    void showError(const QString &serviceId, const QString &detail);
    void armWatch(const QString &serviceId);
    void scheduleAttach();
    void schedulePump();
    void noteCdpDown();

    Guide *m_guide = nullptr;
    ChromeSession *m_chrome = nullptr;
    CdpClient *m_cdp = nullptr;
    QString m_dataDir;
    QString m_cacheDir;
    QVector<MirrorAdapter> m_adapters;
    QStringList m_queue;
    QString m_current;
    QString m_playerService;
    QString m_pendingUrl;
    QString m_navUrl;
    QStringList m_extraLeft;
    QVector<CoverJob> m_covers;
    QVector<CoverJob> m_coverBatch;
    QVector<CoverJob> m_clipJobs;
    QVector<CoverShot> m_shots;
    QVector<HeldImage> m_images;
    QVector<NetworkSample> m_samples;
    QHash<QString, QString> m_inflight;
    QHash<QString, QString> m_imageInflight;
    QSet<QString> m_coverAttempted;
    QSet<QString> m_artLogged;
    QSet<QString> m_coverLogged;
    QHash<QString, QVector<ShelfData>> m_pages;
    QSet<QString> m_learned;
    QSet<QString> m_rolled;
    QString m_domSnap;
    QNetworkAccessManager *m_http = nullptr;
    QTimer *m_refresh = nullptr;
    QHash<QString, int> m_loadToken;
    QString m_tabService;
    QString m_openUrl;
    int m_lastFeedTitles = -1;
    int m_lastDomTitles = -1;
    int m_job = 0;
    int m_attachTries = 0;
    bool m_cdpGaveUp = false;
    qint64 m_busySince = 0;
    bool m_busy = false;
    bool m_player = false;
    bool m_started = false;
    bool m_askedLogin = false;
    bool m_downloading = false;
    bool m_learnPosted = false;
    bool m_mergePage = false;
    bool m_holdNav = false;
    bool m_resumeExtra = false;
    bool m_forceReload = false;
    bool m_sawPlayback = false;
    int m_epoch = 0;
    int m_playNavTries = 0;
    int m_coverGen = 0;
    int m_coverPhase = 0;
    int m_coverFetched = 0;
    int m_coverFromNet = 0;
    int m_coverFromFetch = 0;
    int m_coverFromClip = 0;
    int m_shotIndex = 0;
    bool m_coverFetchGaveUp = false;
    bool m_coverClosed = false;
    bool m_deniedFetch = false;
    QVector<CoverJob> m_deniedCovers;
    QString m_coverError;
    QString m_coverSample;
    QVector<CoverJob> m_directJobs;
    int m_directActive = 0;
    int m_directSaved = 0;
    int m_directCards = 0;
    int m_directFromNet = 0;
    int m_directTotal = 0;
    MirrorPage m_feedPage;
    bool m_feedPending = false;
    QString m_directError;
    QString m_directFile;
};

} // namespace zappe
