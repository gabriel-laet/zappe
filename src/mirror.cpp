#include "mirror.h"

#include "cdp.h"
#include "chrome.h"
#include "guide.h"
#include "posters.h"
#include "util.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <cstdio>

namespace zappe {
namespace {

enum Job { JobNone = 0, JobExtract = 1, JobSearch = 2, JobPlay = 3, JobProfile = 4 };

bool envOn(const char *name)
{
    const QByteArray value = qgetenv(name);
    return !value.isEmpty() && value != "0" && value != "false" && value != "off";
}

QString opt(const QJsonObject &obj, const char *key)
{
    const QJsonValue value = obj.value(QLatin1String(key));
    return value.isString() ? value.toString() : QString();
}

bool continueRow(const MirrorAdapter &adapter, const QString &title, const QString &context)
{
    const QString blob = (title + QLatin1Char(' ') + context).toLower();
    for (const QString &hint : adapter.continueHints) {
        if (!hint.isEmpty() && blob.contains(hint.toLower()))
            return true;
    }
    return false;
}

void paintMissingCovers(QVector<ShelfData> &shelves, const QString &cacheDir, const QDateTime &now)
{
    for (ShelfData &shelf : shelves) {
        for (RowData &row : shelf.rows) {
            if (!row.artworkSpec.startsWith(QStringLiteral("http")))
                continue;
            const QString path = coverCachePath(cacheDir, row.artworkSpec);
            if (coverIsFresh(path, now, mirrorCoverMaxAgeSecs())) {
                row.artwork = path;
                continue;
            }
            const ArtPaths art = makePoster(cacheDir, row.title, row.serviceLabel, row.wide);
            row.artwork = art.artwork;
            row.ambient = art.ambient;
            row.color = art.color;
        }
    }
}

} // namespace

int mirrorCoverMaxAgeSecs()
{
    return 12 * 60 * 60;
}

int mirrorRefreshMs()
{
    return 20 * 60 * 1000;
}

bool mirrorChromeInFront(bool playerOpen)
{
    return playerOpen;
}

QVector<MirrorAdapter> parseMirrorAdapters(const QByteArray &json)
{
    QVector<MirrorAdapter> adapters;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return adapters;
    for (const QJsonValue &value : doc.object().value(QStringLiteral("services")).toArray()) {
        const QJsonObject obj = value.toObject();
        MirrorAdapter adapter;
        adapter.id = opt(obj, "id");
        adapter.name = opt(obj, "name");
        adapter.kind = opt(obj, "kind");
        if (adapter.kind.isEmpty())
            adapter.kind = QStringLiteral("generic");
        adapter.homeUrl = opt(obj, "homeUrl");
        adapter.searchUrl = opt(obj, "searchUrl");
        adapter.rowSelector = opt(obj, "rowSelector");
        adapter.rowTitleSelector = opt(obj, "rowTitleSelector");
        adapter.cardSelector = opt(obj, "cardSelector");
        adapter.linkSelector = opt(obj, "linkSelector");
        adapter.imageSelector = opt(obj, "imageSelector");
        adapter.progressSelector = opt(obj, "progressSelector");
        adapter.profileSelector = opt(obj, "profileSelector");
        for (const QJsonValue &hint : obj.value(QStringLiteral("continueHints")).toArray()) {
            const QString text = hint.toString().trimmed();
            if (!text.isEmpty())
                adapter.continueHints.append(text);
        }
        if (adapter.id.isEmpty() || adapter.homeUrl.isEmpty() || adapter.rowSelector.isEmpty())
            continue;
        adapters.append(adapter);
    }
    return adapters;
}

QVector<MirrorAdapter> loadMirrorAdapters()
{
    QFile file(QStringLiteral(":/zappe/data/mirrors.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return parseMirrorAdapters(file.readAll());
}

const MirrorAdapter *findMirrorAdapter(const QVector<MirrorAdapter> &adapters, const QString &id)
{
    for (const MirrorAdapter &adapter : adapters) {
        if (adapter.id.compare(id, Qt::CaseInsensitive) == 0)
            return &adapter;
    }
    return nullptr;
}

QString mirrorExtractScript(const MirrorAdapter &adapter)
{
    QStringList hints;
    for (const QString &hint : adapter.continueHints)
        hints.append(jsString(hint));
    const QString cfg =
        QStringLiteral("{row:%1,title:%2,card:%3,link:%4,image:%5,progress:%6,profile:%7,hints:[%8],maxRows:%9")
            .arg(jsString(adapter.rowSelector), jsString(adapter.rowTitleSelector), jsString(adapter.cardSelector),
                 jsString(adapter.linkSelector), jsString(adapter.imageSelector), jsString(adapter.progressSelector),
                 jsString(adapter.profileSelector), hints.join(QLatin1Char(',')), QString::number(adapter.limitRows))
        + QStringLiteral(",maxCards:") + QString::number(adapter.limitCards) + QLatin1Char('}');
    return QStringLiteral("(() => { const cfg = ") + cfg + QStringLiteral(R"js(;
  const abs = (href) => { try { return new URL(href, location.href).href; } catch (e) { return href || ''; } };
  const label = (el) => {
    if (!el) return '';
    const aria = (el.getAttribute && (el.getAttribute('aria-label') || el.getAttribute('alt'))) || '';
    const text = ((el.innerText || el.textContent || '') + '').replace(/\s+/g, ' ').trim();
    return (aria || text).replace(/\s+/g, ' ').trim();
  };
  const artOf = (img) => img ? (img.currentSrc || img.src || img.getAttribute('src') || '') : '';
  const progressOf = (el) => {
    if (!el) return 0;
    const now = parseFloat(el.getAttribute('aria-valuenow') || '');
    const max = parseFloat(el.getAttribute('aria-valuemax') || '100');
    if (!Number.isNaN(now) && max > 0) return Math.max(0, Math.min(1, now / max));
    const w = (el.style && el.style.width) || '';
    const m = /([\d.]+)%/.exec(w);
    if (m) return Math.max(0, Math.min(1, parseFloat(m[1]) / 100));
    return 0;
  };
  try {
    const profiles = [...document.querySelectorAll(cfg.profile)].map(label).filter(Boolean);
    const shelves = [];
    const seenRow = new Set();
    for (const rowEl of document.querySelectorAll(cfg.row)) {
      if (shelves.length >= cfg.maxRows) break;
      const context = rowEl.getAttribute('data-list-context') || '';
      const title = label(rowEl.querySelector(cfg.title)) || context;
      const rows = [];
      const seen = new Set();
      for (const card of rowEl.querySelectorAll(cfg.card)) {
        if (rows.length >= cfg.maxCards) break;
        const link = (card.matches && card.matches('a[href]')) ? card : (card.querySelector(cfg.link) || card.querySelector('a[href]'));
        const img = card.querySelector(cfg.image);
        const href = abs(link && link.getAttribute('href'));
        const name = label(link) || label(img) || label(card);
        if (!href || !name || seen.has(href)) continue;
        seen.add(href);
        rows.push({title: name, href: href, artwork: artOf(img), progress: progressOf(card.querySelector(cfg.progress))});
      }
      if (!rows.length) continue;
      const key = (title || 'row') + ':' + rows[0].href;
      if (seenRow.has(key)) continue;
      seenRow.add(key);
      shelves.push({title: title, context: context, rows: rows});
    }
    const login = !!document.querySelector("input[type='password'], form[action*='login']");
    return JSON.stringify({needsLogin: login && shelves.length === 0, profiles: profiles, shelves: shelves});
  } catch (e) {
    return JSON.stringify({needsLogin: false, profiles: [], shelves: [], error: String(e)});
  }
})())js");
}

QString mirrorFocusPlayerScript()
{
    return QStringLiteral("(() => { const video = document.querySelector('video');"
                          " if (video) { try { video.focus(); } catch (e) {} }"
                          " if (document.body) { try { document.body.focus(); } catch (e) {} }"
                          " return 'ok'; })()");
}

QString mirrorSearchUrl(const MirrorAdapter &adapter, const QString &query)
{
    const QString text = query.trimmed();
    if (adapter.searchUrl.isEmpty() || text.isEmpty())
        return {};
    return QString(adapter.searchUrl).replace(QStringLiteral("{q}"), QString::fromUtf8(QUrl::toPercentEncoding(text)));
}

QString mirrorPlayUrl(const MirrorAdapter &adapter, const QString &href)
{
    const QString trimmed = href.trimmed();
    if (trimmed.startsWith(QStringLiteral("zappe://")))
        return trimmed;
    const QUrl url(trimmed);
    if (!url.isValid() || url.scheme().isEmpty())
        return {};
    if (adapter.kind == QLatin1String("netflix")) {
        const QRegularExpression watch(QStringLiteral("/watch/(\\d+)"));
        const QRegularExpression title(QStringLiteral("/title/(\\d+)"));
        QRegularExpressionMatch match = watch.match(url.path());
        if (!match.hasMatch())
            match = title.match(url.path());
        if (!match.hasMatch())
            return {};
        return QStringLiteral("https://www.netflix.com/watch/%1").arg(match.captured(1));
    }
    if (adapter.kind == QLatin1String("youtube")) {
        QUrlQuery query(url);
        QString id = query.queryItemValue(QStringLiteral("v"));
        if (id.isEmpty() && url.host().contains(QStringLiteral("youtu.be")))
            id = url.path().mid(1);
        if (id.isEmpty()) {
            const QRegularExpression shorts(QStringLiteral("/shorts/([\\w-]{6,})"));
            const QRegularExpressionMatch match = shorts.match(url.path());
            if (match.hasMatch())
                id = match.captured(1);
        }
        if (id.isEmpty())
            return {};
        return QStringLiteral("https://www.youtube.com/watch?v=%1").arg(id);
    }
    if (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))
        return {};
    const QString path = url.path().toLower();
    const bool play = path.contains(QStringLiteral("/watch")) || path.contains(QStringLiteral("/play"))
                      || path.contains(QStringLiteral("/player")) || path.contains(QStringLiteral("/video"))
                      || path.contains(QStringLiteral("/v/"));
    return play ? trimmed : QString();
}

bool mirrorIsPlayable(const MirrorAdapter &adapter, const QString &href)
{
    const QString play = mirrorPlayUrl(adapter, href);
    return !play.isEmpty() && !play.startsWith(QStringLiteral("zappe://"));
}

MirrorPage parseMirrorPage(const QByteArray &json, const MirrorAdapter &adapter)
{
    MirrorPage page;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return page;
    const QJsonObject root = doc.object();
    page.ok = true;
    for (const QJsonValue &value : root.value(QStringLiteral("profiles")).toArray()) {
        const QString name = value.toString().trimmed();
        if (!name.isEmpty() && !page.profiles.contains(name))
            page.profiles.append(name);
    }
    const QString skill = QStringLiteral("mirror:") + adapter.id;
    if (!page.profiles.isEmpty()) {
        ShelfData profiles;
        profiles.id = skill + QStringLiteral(":profiles");
        profiles.skillId = skill;
        profiles.title = QStringLiteral("Perfis");
        profiles.status = QStringLiteral("ok");
        for (const QString &name : page.profiles) {
            RowData row;
            row.title = name;
            row.service = adapter.id;
            row.serviceLabel = adapter.name;
            row.href = QStringLiteral("zappe://profile/") + QString::fromUtf8(QUrl::toPercentEncoding(name));
            profiles.rows.append(row);
        }
        page.shelves.append(profiles);
    }
    int index = 0;
    int content = 0;
    for (const QJsonValue &shelfValue : root.value(QStringLiteral("shelves")).toArray()) {
        if (content >= adapter.limitRows)
            break;
        const QJsonObject shelfObj = shelfValue.toObject();
        const QString title = shelfObj.value(QStringLiteral("title")).toString().trimmed();
        const QString context = shelfObj.value(QStringLiteral("context")).toString();
        const bool cont = continueRow(adapter, title, context);
        ShelfData shelf;
        shelf.id = skill + QLatin1Char(':') + (cont ? QStringLiteral("continue") : QString::number(index));
        shelf.skillId = skill;
        shelf.title = title.isEmpty() ? (cont ? QStringLiteral("Continuar assistindo") : QStringLiteral("Em alta")) : title;
        shelf.status = QStringLiteral("ok");
        for (const QJsonValue &rowValue : shelfObj.value(QStringLiteral("rows")).toArray()) {
            if (shelf.rows.size() >= adapter.limitCards)
                break;
            const QJsonObject rowObj = rowValue.toObject();
            const QString href = mirrorPlayUrl(adapter, rowObj.value(QStringLiteral("href")).toString());
            QString name = rowObj.value(QStringLiteral("title")).toString().trimmed();
            if (name.isEmpty() || href.isEmpty() || href.startsWith(QStringLiteral("zappe://")))
                continue;
            RowData row;
            row.title = name;
            row.service = adapter.id;
            row.serviceLabel = adapter.name;
            row.href = href;
            row.artworkSpec = rowObj.value(QStringLiteral("artwork")).toString();
            row.wide = cont;
            row.progress = rowObj.value(QStringLiteral("progress")).toDouble();
            if (row.progress < 0)
                row.progress = 0;
            if (row.progress > 1)
                row.progress = 1;
            shelf.rows.append(row);
        }
        if (shelf.rows.isEmpty())
            continue;
        page.shelves.append(shelf);
        if (!cont)
            ++index;
        ++content;
    }
    page.needsLogin = root.value(QStringLiteral("needsLogin")).toBool() && content == 0;
    return page;
}

QString coverCachePath(const QString &cacheDir, const QString &url)
{
    const QByteArray hash = QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex();
    QDir dir(cacheDir);
    dir.mkpath(QStringLiteral("covers"));
    return dir.filePath(QStringLiteral("covers/%1.img").arg(QString::fromLatin1(hash)));
}

bool coverIsFresh(const QString &path, const QDateTime &now, int maxAgeSecs)
{
    const QFileInfo info(path);
    if (!info.exists() || info.size() < 32)
        return false;
    const qint64 age = info.lastModified().secsTo(now);
    return age >= -60 && age <= maxAgeSecs;
}

QVector<CoverJob> coverJobs(const QVector<ShelfData> &shelves, const QString &cacheDir, const QDateTime &now)
{
    QVector<CoverJob> jobs;
    QSet<QString> seen;
    for (const ShelfData &shelf : shelves) {
        for (const RowData &row : shelf.rows) {
            if (!row.artworkSpec.startsWith(QStringLiteral("http")) || seen.contains(row.artworkSpec))
                continue;
            const QString path = coverCachePath(cacheDir, row.artworkSpec);
            if (coverIsFresh(path, now, mirrorCoverMaxAgeSecs()))
                continue;
            seen.insert(row.artworkSpec);
            jobs.append({shelf.skillId.mid(QStringLiteral("mirror:").size()), row.artworkSpec, path});
        }
    }
    return jobs;
}

MirrorSession::MirrorSession(QObject *parent)
    : QObject(parent)
    , m_http(new QNetworkAccessManager(this))
{
}

void MirrorSession::start(Guide *guide, ChromeSession *chrome, const QString &dataDir)
{
    if (m_started || envOn("ZAPPE_MIRROR_DISABLE"))
        return;
    m_adapters = loadMirrorAdapters();
    if (m_adapters.isEmpty())
        return;
    if (findChromeBinary().isEmpty()) {
        std::fprintf(stderr, "zappe: espelho sem Chrome; o guia segue sem as fileiras dos streamings\n");
        m_adapters.clear();
        return;
    }
    m_started = true;
    m_guide = guide;
    m_chrome = chrome;
    m_dataDir = dataDir;
    m_cacheDir = cacheDir();
    m_cdp = new CdpClient(this);
    connect(m_cdp, &CdpClient::attached, this, &MirrorSession::onAttached);
    connect(m_cdp, &CdpClient::navigated, this, &MirrorSession::onNavigated);
    connect(m_cdp, &CdpClient::evaluated, this, &MirrorSession::onEvaluated);
    connect(m_cdp, &CdpClient::failed, this, [this](const QString &message) {
        if (m_job == JobPlay)
            emit failed(message);
        m_busy = false;
        if (!m_player)
            QTimer::singleShot(800, this, &MirrorSession::pump);
    });
    connect(m_chrome, &ChromeSession::ready, this, &MirrorSession::onChromeReady);
    connect(m_chrome, &ChromeSession::failed, this, [this](const QString &message) { emit failed(message); });
    m_refresh = new QTimer(this);
    m_refresh->setInterval(mirrorRefreshMs());
    connect(m_refresh, &QTimer::timeout, this, [this] {
        if (m_player)
            return;
        m_queue.clear();
        for (const MirrorAdapter &adapter : m_adapters)
            m_queue.append(adapter.id);
        pump();
    });
    m_refresh->start();
    m_chrome->ensure(m_dataDir);
}

void MirrorSession::onChromeReady()
{
    if (!m_cdp || !m_chrome)
        return;
    m_cdp->attach(m_chrome->port());
}

void MirrorSession::onAttached()
{
    if (!m_pendingUrl.isEmpty() && m_job == JobPlay) {
        const QString url = m_pendingUrl;
        m_cdp->navigate(url);
        return;
    }
    if (m_queue.isEmpty()) {
        for (const MirrorAdapter &adapter : m_adapters)
            m_queue.append(adapter.id);
    }
    pump();
}

void MirrorSession::showLoading(const QString &serviceId)
{
    const MirrorAdapter *adapter = findMirrorAdapter(m_adapters, serviceId);
    if (!m_guide || !adapter)
        return;
    if (m_pages.contains(serviceId) && !m_pages.value(serviceId).isEmpty())
        return;
    ShelfData shelf;
    shelf.id = QStringLiteral("mirror:%1:loading").arg(serviceId);
    shelf.skillId = QStringLiteral("mirror:") + serviceId;
    shelf.title = adapter->name;
    shelf.status = QStringLiteral("loading");
    shelf.message = QStringLiteral("Atualizando…");
    m_guide->replaceServiceShelves(serviceId, {shelf});
}

void MirrorSession::refresh(const QString &serviceId)
{
    if (!findMirrorAdapter(m_adapters, serviceId))
        return;
    showLoading(serviceId);
    m_queue.removeAll(serviceId);
    m_queue.prepend(serviceId);
    pump();
}

void MirrorSession::search(const QString &serviceId, const QString &query)
{
    const MirrorAdapter *adapter = findMirrorAdapter(m_adapters, serviceId);
    if (!adapter)
        return;
    const QString url = mirrorSearchUrl(*adapter, query);
    if (url.isEmpty())
        return;
    showLoading(serviceId);
    begin(serviceId, url, JobSearch);
}

void MirrorSession::chooseProfile(const QString &serviceId, const QString &name)
{
    if (!m_cdp || !m_cdp->connected() || name.trimmed().isEmpty())
        return;
    m_current = serviceId;
    m_job = JobProfile;
    m_busy = true;
    m_cdp->evaluate(profileScript(name.trimmed()));
}

void MirrorSession::play(const QString &href)
{
    m_player = true;
    m_busy = true;
    m_job = JobPlay;
    m_pendingUrl = href;
    m_queue.clear();
    if (!m_cdp || !m_cdp->connected())
        return;
    m_cdp->navigate(href);
}

void MirrorSession::leavePlayer()
{
    const bool was = m_player;
    m_player = false;
    m_busy = false;
    m_job = JobNone;
    m_pendingUrl.clear();
    if (was && m_cdp && m_cdp->connected())
        m_cdp->dispatchAction(QStringLiteral("back"));
    if (!m_current.isEmpty())
        m_queue.prepend(m_current);
    QTimer::singleShot(1200, this, &MirrorSession::pump);
}

void MirrorSession::dispatchAction(const QString &action)
{
    if (m_cdp)
        m_cdp->dispatchAction(action);
}

void MirrorSession::focusPlayer()
{
    if (m_cdp && m_cdp->connected())
        m_cdp->evaluate(mirrorFocusPlayerScript());
}

void MirrorSession::begin(const QString &serviceId, const QString &url, int job)
{
    if (m_player || url.isEmpty())
        return;
    if (!m_cdp || !m_cdp->connected()) {
        m_queue.prepend(serviceId);
        return;
    }
    m_current = serviceId;
    m_job = job;
    m_busy = true;
    m_cdp->navigate(url);
}

void MirrorSession::pump()
{
    if (m_player || m_busy || m_queue.isEmpty() || !m_cdp || !m_cdp->connected())
        return;
    const QString serviceId = m_queue.takeFirst();
    const MirrorAdapter *adapter = findMirrorAdapter(m_adapters, serviceId);
    if (!adapter)
        return;
    begin(serviceId, adapter->homeUrl, JobExtract);
}

void MirrorSession::onNavigated()
{
    if (m_job == JobPlay) {
        m_busy = false;
        emit playerReady();
        return;
    }
    if (m_job != JobExtract && m_job != JobSearch)
        return;
    const QString serviceId = m_current;
    const int job = m_job;
    QTimer::singleShot(1800, this, [this, serviceId, job] {
        if (m_player || m_current != serviceId || m_job != job || !m_cdp)
            return;
        const MirrorAdapter *adapter = findMirrorAdapter(m_adapters, serviceId);
        if (!adapter)
            return;
        m_cdp->evaluate(mirrorExtractScript(*adapter));
    });
}

void MirrorSession::onEvaluated(const QString &value, bool ok)
{
    if (m_job == JobProfile) {
        m_busy = false;
        m_job = JobNone;
        if (!m_current.isEmpty())
            refresh(m_current);
        return;
    }
    if (m_job != JobExtract && m_job != JobSearch)
        return;
    const MirrorAdapter *adapter = findMirrorAdapter(m_adapters, m_current);
    m_busy = false;
    m_job = JobNone;
    if (!adapter || !ok) {
        QTimer::singleShot(600, this, &MirrorSession::pump);
        return;
    }
    const MirrorPage page = parseMirrorPage(value.toUtf8(), *adapter);
    if (page.needsLogin && !m_askedLogin) {
        m_askedLogin = true;
        emit needsLogin(adapter->id);
    }
    applyPage(page);
    QTimer::singleShot(500, this, &MirrorSession::pump);
}

void MirrorSession::applyPage(const MirrorPage &page)
{
    if (!m_guide || m_current.isEmpty())
        return;
    const MirrorAdapter *adapter = findMirrorAdapter(m_adapters, m_current);
    if (!adapter)
        return;
    QVector<ShelfData> shelves = page.shelves;
    if (shelves.isEmpty() && !page.needsLogin) {
        ShelfData empty;
        empty.id = QStringLiteral("mirror:%1:empty").arg(adapter->id);
        empty.skillId = QStringLiteral("mirror:") + adapter->id;
        empty.title = adapter->name;
        empty.status = QStringLiteral("empty");
        empty.message = QStringLiteral("Nada nesta página ainda.");
        shelves.append(empty);
    }
    if (shelves.isEmpty())
        return;
    paintMissingCovers(shelves, m_cacheDir, QDateTime::currentDateTime());
    m_pages.insert(adapter->id, shelves);
    m_guide->replaceServiceShelves(adapter->id, shelves);
    const QVector<CoverJob> jobs = coverJobs(shelves, m_cacheDir, QDateTime::currentDateTime());
    for (const CoverJob &job : jobs) {
        bool queued = false;
        for (const CoverJob &have : m_covers) {
            if (have.url == job.url)
                queued = true;
        }
        if (!queued)
            m_covers.append(CoverJob{adapter->id, job.url, job.path});
    }
    downloadNext();
}

void MirrorSession::downloadNext()
{
    if (m_downloading || m_covers.isEmpty())
        return;
    m_downloading = true;
    const CoverJob job = m_covers.takeFirst();
    QNetworkRequest request{QUrl(job.url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Zappe"));
    QNetworkReply *reply = m_http->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, job] {
        const QByteArray body = reply->readAll();
        const bool httpOk = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        QImage image;
        if (httpOk)
            image.loadFromData(body);
        if (!image.isNull()) {
            QDir().mkpath(QFileInfo(job.path).absolutePath());
            QFile out(job.path);
            if (out.open(QIODevice::WriteOnly))
                out.write(body);
            QVector<ShelfData> shelves = m_pages.value(job.serviceId);
            bool changed = false;
            for (ShelfData &shelf : shelves) {
                for (RowData &row : shelf.rows) {
                    if (row.artworkSpec == job.url) {
                        row.artwork = job.path;
                        changed = true;
                    }
                }
            }
            if (changed && m_guide) {
                m_pages.insert(job.serviceId, shelves);
                m_guide->replaceServiceShelves(job.serviceId, shelves);
            }
        }
        m_downloading = false;
        downloadNext();
    });
}

} // namespace zappe
