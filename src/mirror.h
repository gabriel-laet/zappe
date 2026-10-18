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

MirrorPage parseMirrorPage(const QByteArray &json, const MirrorAdapter &adapter);
MirrorPage parseMirrorFeeds(const QVector<NetworkSample> &samples, const MirrorAdapter &adapter);
bool mirrorPageHasTitles(const MirrorPage &page);
int mirrorTitleCount(const MirrorPage &page);
bool mirrorPageUsable(const MirrorPage &page, int minTitles);

QString coverCachePath(const QString &cacheDir, const QString &url);
bool coverIsFresh(const QString &path, const QDateTime &now, int maxAgeSecs);
int mirrorCoverMaxAgeSecs();
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
    void finishEmpty();
    void applyPage(const MirrorPage &page);
    void downloadNext();
    void showLoading(const QString &serviceId);
    void showError(const QString &serviceId, const QString &detail);
    void armWatch(const QString &serviceId);
    void scheduleAttach();
    void schedulePump();

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
    QVector<NetworkSample> m_samples;
    QHash<QString, QString> m_inflight;
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
    qint64 m_busySince = 0;
    bool m_busy = false;
    bool m_player = false;
    bool m_started = false;
    bool m_askedLogin = false;
    bool m_downloading = false;
    bool m_learnPosted = false;
    bool m_mergePage = false;
    bool m_holdNav = false;
    int m_epoch = 0;
};

} // namespace zappe
