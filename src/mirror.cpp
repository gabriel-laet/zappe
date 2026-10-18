#include "mirror.h"

#include "cdp.h"
#include "chrome.h"
#include "guide.h"
#include "learn.h"
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

#include <algorithm>
#include <cstdio>

namespace zappe {
namespace {

enum Job { JobNone = 0, JobExtract = 1, JobSearch = 2, JobPlay = 3, JobProfile = 4, JobLearnDom = 5, JobLearnShot = 6 };

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

bool titleRejected(const MirrorAdapter &adapter, const QString &title)
{
    const QString lower = title.toLower();
    for (const QString &hint : adapter.rejectHints) {
        if (!hint.isEmpty() && lower.contains(hint.toLower()))
            return true;
    }
    return false;
}

QStringList readStrings(const QJsonValue &value)
{
    QStringList out;
    for (const QJsonValue &item : value.toArray()) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty())
            out.append(text);
    }
    return out;
}

QString jsonText(const QJsonValue &value, int depth)
{
    if (depth > 4 || value.isNull() || value.isUndefined())
        return QString();
    if (value.isString())
        return value.toString().simplified();
    if (!value.isObject())
        return QString();
    const QJsonObject obj = value.toObject();
    if (obj.contains(QStringLiteral("simpleText")))
        return jsonText(obj.value(QStringLiteral("simpleText")), depth + 1);
    const QJsonValue runs = obj.value(QStringLiteral("runs"));
    if (runs.isArray()) {
        QStringList parts;
        for (const QJsonValue &run : runs.toArray()) {
            const QJsonValue text = run.isObject() ? run.toObject().value(QStringLiteral("text")) : run;
            const QString part = jsonText(text, depth + 1);
            if (!part.isEmpty())
                parts.append(part);
        }
        return parts.join(QString()).simplified();
    }
    if (obj.value(QStringLiteral("text")).isString())
        return obj.value(QStringLiteral("text")).toString().simplified();
    return QString();
}

QString jsonToken(const QJsonValue &value)
{
    if (value.isDouble()) {
        const qint64 number = qRound64(value.toDouble());
        if (number >= 10000)
            return QString::number(number);
        return QString();
    }
    if (value.isString()) {
        const QString text = value.toString().trimmed();
        return text.size() > 200 ? QString() : text;
    }
    if (!value.isObject())
        return QString();
    const QJsonObject obj = value.toObject();
    const QStringList keys = {QStringLiteral("watchId"), QStringLiteral("videoId"), QStringLiteral("id")};
    for (const QString &key : keys) {
        if (!obj.contains(key))
            continue;
        const QString token = jsonToken(obj.value(key));
        if (!token.isEmpty())
            return token;
    }
    return QString();
}

QString jsonArt(const QJsonValue &value, int depth)
{
    if (depth > 5)
        return QString();
    if (value.isString()) {
        const QString text = value.toString().trimmed();
        if (text.startsWith(QStringLiteral("http://")) || text.startsWith(QStringLiteral("https://")))
            return text;
        return QString();
    }
    if (value.isArray()) {
        QString best;
        for (const QJsonValue &child : value.toArray()) {
            const QString art = jsonArt(child, depth + 1);
            if (!art.isEmpty())
                best = art;
        }
        return best;
    }
    if (!value.isObject())
        return QString();
    const QJsonObject obj = value.toObject();
    if (obj.contains(QStringLiteral("thumbnails")))
        return jsonArt(obj.value(QStringLiteral("thumbnails")), depth + 1);
    if (obj.contains(QStringLiteral("url")))
        return jsonArt(obj.value(QStringLiteral("url")), depth + 1);
    if (obj.contains(QStringLiteral("src")))
        return jsonArt(obj.value(QStringLiteral("src")), depth + 1);
    return QString();
}

QString firstText(const QJsonObject &obj, const QStringList &keys)
{
    for (const QString &key : keys) {
        if (!obj.contains(key))
            continue;
        const QString text = jsonText(obj.value(key), 0);
        if (!text.isEmpty())
            return text;
    }
    return QString();
}

QString firstToken(const QJsonObject &obj, const QStringList &keys)
{
    for (const QString &key : keys) {
        if (!obj.contains(key))
            continue;
        const QString token = jsonToken(obj.value(key));
        if (!token.isEmpty())
            return token;
    }
    return QString();
}

QString firstArt(const QJsonObject &obj, const QStringList &keys)
{
    for (const QString &key : keys) {
        if (!obj.contains(key))
            continue;
        const QString art = jsonArt(obj.value(key), 0);
        if (!art.isEmpty())
            return art;
    }
    return QString();
}

bool feedMatches(const MirrorFeed &feed, const QString &url)
{
    if (url.isEmpty() || feed.urlContains.isEmpty())
        return true;
    const QString lower = url.toLower();
    for (const QString &part : feed.urlContains) {
        if (!part.isEmpty() && lower.contains(part.toLower()))
            return true;
    }
    return false;
}

QString applyHref(const MirrorAdapter &adapter, const MirrorFeed &feed, const QString &href)
{
    const QString trimmed = href.trimmed();
    if (trimmed.startsWith(QStringLiteral("http://")) || trimmed.startsWith(QStringLiteral("https://")))
        return trimmed;
    if (trimmed.startsWith(QLatin1Char('/')) && !adapter.homeUrl.isEmpty()) {
        const QUrl absolute = QUrl(adapter.homeUrl).resolved(QUrl(trimmed));
        if (absolute.isValid() && absolute.scheme().startsWith(QStringLiteral("http")))
            return absolute.toString();
    }
    static const QRegularExpression idRe(QStringLiteral("^[A-Za-z0-9_-]{4,32}$"));
    if (feed.hrefTemplate.contains(QStringLiteral("{href}")) && idRe.match(trimmed).hasMatch())
        return QString(feed.hrefTemplate).replace(QStringLiteral("{href}"), trimmed);
    return QString();
}

struct FeedItem {
    QString title;
    QString href;
    QString artwork;
    QString shelf;
};

bool walkFeed(const QJsonValue &value, const QString &parentShelf, const MirrorAdapter &adapter, const MirrorFeed &feed,
              QVector<FeedItem> &out, int depth)
{
    if (out.size() >= 200)
        return true;
    if (depth > 12 || (!value.isArray() && !value.isObject()))
        return false;
    if (value.isArray()) {
        bool nested = false;
        for (const QJsonValue &child : value.toArray())
            nested = walkFeed(child, parentShelf, adapter, feed, out, depth + 1) || nested;
        return nested;
    }
    const QJsonObject obj = value.toObject();
    QString childShelf = parentShelf;
    for (const QString &key : feed.shelfKeys) {
        const QString text = jsonText(obj.value(key), 0);
        if (text.isEmpty() || text.size() > 80)
            continue;
        childShelf = text;
        break;
    }
    bool nested = false;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        nested = walkFeed(it.value(), childShelf, adapter, feed, out, depth + 1) || nested;
    if (nested)
        return true;
    QJsonObject view = obj;
    if (obj.value(QStringLiteral("attributes")).isObject()) {
        const QJsonObject attrs = obj.value(QStringLiteral("attributes")).toObject();
        for (auto it = attrs.constBegin(); it != attrs.constEnd(); ++it) {
            if (!view.contains(it.key()))
                view.insert(it.key(), it.value());
        }
    }
    const QString title = firstText(view, feed.titleKeys);
    const QString hrefRaw = firstToken(view, feed.hrefKeys);
    if (title.size() < 2 || title.size() > 140 || hrefRaw.isEmpty())
        return false;
    const QString href = applyHref(adapter, feed, hrefRaw);
    if (href.isEmpty())
        return false;
    FeedItem item;
    item.title = title;
    item.href = href;
    item.artwork = firstArt(view, feed.artworkKeys);
    item.shelf = parentShelf.compare(title, Qt::CaseInsensitive) == 0 ? QString() : parentShelf;
    out.append(item);
    return true;
}

bool matchesAdapterFeed(const QVector<MirrorAdapter> &adapters, const QString &serviceId, const QString &url)
{
    const MirrorAdapter *adapter = findMirrorAdapter(adapters, serviceId);
    if (!adapter)
        return false;
    for (const MirrorFeed &feed : adapter->feeds) {
        if (!feed.urlContains.isEmpty() && feedMatches(feed, url))
            return true;
    }
    return false;
}

void evictNonFeed(QVector<NetworkSample> &samples, const QVector<MirrorAdapter> &adapters, const QString &serviceId)
{
    for (int i = 0; i < samples.size(); ++i) {
        if (!matchesAdapterFeed(adapters, serviceId, samples.at(i).url)) {
            samples.removeAt(i);
            return;
        }
    }
}

bool wantNetworkBody(const QVector<MirrorAdapter> &adapters, const QString &serviceId, const QString &url, const QString &mime)
{
    const QString mimeLower = mime.toLower();
    const QString urlLower = url.toLower();
    if (urlLower.contains(QStringLiteral(".js")) || urlLower.contains(QStringLiteral(".css"))
        || mimeLower.contains(QStringLiteral("javascript")) || mimeLower.contains(QStringLiteral("text/html"))
        || mimeLower.startsWith(QStringLiteral("image/")))
        return false;
    const bool json = mimeLower.contains(QStringLiteral("json")) || urlLower.contains(QStringLiteral("graphql"));
    const MirrorAdapter *adapter = findMirrorAdapter(adapters, serviceId);
    if (adapter) {
        for (const MirrorFeed &feed : adapter->feeds) {
            if (!feed.urlContains.isEmpty() && feedMatches(feed, url))
                return true;
        }
    }
    return json;
}

QJsonArray stringArray(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values)
        array.append(value);
    return array;
}

MirrorFeed readFeed(const QJsonObject &obj)
{
    MirrorFeed feed;
    feed.urlContains = readStrings(obj.value(QStringLiteral("urlContains")));
    feed.titleKeys = readStrings(obj.value(QStringLiteral("titleKeys")));
    feed.hrefKeys = readStrings(obj.value(QStringLiteral("hrefKeys")));
    feed.artworkKeys = readStrings(obj.value(QStringLiteral("artworkKeys")));
    feed.shelfKeys = readStrings(obj.value(QStringLiteral("shelfKeys")));
    feed.hrefTemplate = opt(obj, "hrefTemplate");
    return feed;
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

int mirrorLoadTimeoutMs()
{
    return 20000;
}

bool mirrorShelfRetries(const QString &status, const QString &skillId, QString &serviceId)
{
    serviceId.clear();
    if (status != QLatin1String("error"))
        return false;
    const QString prefix = QStringLiteral("mirror:");
    if (!skillId.startsWith(prefix))
        return false;
    serviceId = skillId.mid(prefix.size());
    return !serviceId.isEmpty();
}

bool mirrorChromeInFront(bool playerOpen)
{
    return playerOpen;
}

bool mirrorReadAdapter(const QJsonObject &obj, MirrorAdapter &adapter)
{
    adapter = MirrorAdapter();
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
    adapter.continueHints = readStrings(obj.value(QStringLiteral("continueHints")));
    adapter.extraUrls = readStrings(obj.value(QStringLiteral("extraUrls")));
    adapter.rejectHints = readStrings(obj.value(QStringLiteral("rejectHints")));
    adapter.playPaths = readStrings(obj.value(QStringLiteral("playPaths")));
    adapter.playClick = opt(obj, "playClick");
    adapter.playerOk = opt(obj, "playerOk");
    adapter.playerSeek = opt(obj, "playerSeek");
    adapter.playerClick = obj.value(QStringLiteral("playerClick")).toBool(false);
    adapter.version = obj.value(QStringLiteral("version")).toInt(0);
    if (obj.value(QStringLiteral("limitRows")).isDouble())
        adapter.limitRows = obj.value(QStringLiteral("limitRows")).toInt(adapter.limitRows);
    if (obj.value(QStringLiteral("limitCards")).isDouble())
        adapter.limitCards = obj.value(QStringLiteral("limitCards")).toInt(adapter.limitCards);
    for (const QJsonValue &value : obj.value(QStringLiteral("feeds")).toArray()) {
        const MirrorFeed feed = readFeed(value.toObject());
        if (!feed.titleKeys.isEmpty() && !feed.hrefKeys.isEmpty())
            adapter.feeds.append(feed);
    }
    return !adapter.id.isEmpty() && !adapter.homeUrl.isEmpty()
           && (!adapter.rowSelector.isEmpty() || !adapter.feeds.isEmpty());
}

QByteArray mirrorAdapterJson(const MirrorAdapter &adapter)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("version"), adapter.version);
    obj.insert(QStringLiteral("id"), adapter.id);
    obj.insert(QStringLiteral("name"), adapter.name);
    obj.insert(QStringLiteral("kind"), adapter.kind);
    obj.insert(QStringLiteral("homeUrl"), adapter.homeUrl);
    obj.insert(QStringLiteral("searchUrl"), adapter.searchUrl);
    obj.insert(QStringLiteral("rowSelector"), adapter.rowSelector);
    obj.insert(QStringLiteral("rowTitleSelector"), adapter.rowTitleSelector);
    obj.insert(QStringLiteral("cardSelector"), adapter.cardSelector);
    obj.insert(QStringLiteral("linkSelector"), adapter.linkSelector);
    obj.insert(QStringLiteral("imageSelector"), adapter.imageSelector);
    obj.insert(QStringLiteral("progressSelector"), adapter.progressSelector);
    obj.insert(QStringLiteral("profileSelector"), adapter.profileSelector);
    obj.insert(QStringLiteral("continueHints"), stringArray(adapter.continueHints));
    obj.insert(QStringLiteral("extraUrls"), stringArray(adapter.extraUrls));
    obj.insert(QStringLiteral("rejectHints"), stringArray(adapter.rejectHints));
    obj.insert(QStringLiteral("playPaths"), stringArray(adapter.playPaths));
    obj.insert(QStringLiteral("playClick"), adapter.playClick);
    obj.insert(QStringLiteral("playerOk"), adapter.playerOk);
    obj.insert(QStringLiteral("playerSeek"), adapter.playerSeek);
    obj.insert(QStringLiteral("playerClick"), adapter.playerClick);
    QJsonArray feeds;
    for (const MirrorFeed &feed : adapter.feeds) {
        QJsonObject item;
        item.insert(QStringLiteral("urlContains"), stringArray(feed.urlContains));
        item.insert(QStringLiteral("titleKeys"), stringArray(feed.titleKeys));
        item.insert(QStringLiteral("hrefKeys"), stringArray(feed.hrefKeys));
        item.insert(QStringLiteral("artworkKeys"), stringArray(feed.artworkKeys));
        item.insert(QStringLiteral("shelfKeys"), stringArray(feed.shelfKeys));
        item.insert(QStringLiteral("hrefTemplate"), feed.hrefTemplate);
        feeds.append(item);
    }
    obj.insert(QStringLiteral("feeds"), feeds);
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QVector<MirrorAdapter> parseMirrorAdapters(const QByteArray &json)
{
    QVector<MirrorAdapter> adapters;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return adapters;
    for (const QJsonValue &value : doc.object().value(QStringLiteral("services")).toArray()) {
        MirrorAdapter adapter;
        if (mirrorReadAdapter(value.toObject(), adapter))
            adapters.append(adapter);
    }
    return adapters;
}

QJsonObject adapterObjectFromFile(const QByteArray &bytes)
{
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject())
        return {};
    const QJsonObject obj = doc.object();
    const QJsonArray services = obj.value(QStringLiteral("services")).toArray();
    if (!services.isEmpty() && services.at(0).isObject())
        return services.at(0).toObject();
    return obj;
}

QVector<MirrorAdapter> loadMirrorAdapters()
{
    QFile file(QStringLiteral(":/zappe/data/mirrors.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return parseMirrorAdapters(file.readAll());
}

QVector<MirrorAdapter> loadMirrorAdapters(const QString &dataDir)
{
    QVector<MirrorAdapter> adapters = loadMirrorAdapters();
    if (dataDir.isEmpty())
        return adapters;
    const QDir dir(QDir(dataDir).filePath(QStringLiteral("adapters")));
    if (!dir.exists())
        return adapters;
    const QStringList names = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &name : names) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject obj = adapterObjectFromFile(file.readAll());
        MirrorAdapter learned;
        if (!mirrorReadAdapter(obj, learned))
            continue;
        bool replaced = false;
        for (MirrorAdapter &adapter : adapters) {
            if (adapter.id.compare(learned.id, Qt::CaseInsensitive) != 0)
                continue;
            if (!obj.contains(QStringLiteral("kind")) && !adapter.kind.isEmpty())
                learned.kind = adapter.kind;
            adapter = learned;
            replaced = true;
            break;
        }
        if (!replaced)
            adapters.append(learned);
    }
    return adapters;
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

QString mirrorFocusPlayerScript(const MirrorAdapter &adapter)
{
    const QString spec = jsString(adapter.playClick);
    const QString click = adapter.playerClick ? QStringLiteral("true") : QStringLiteral("false");
    return QStringLiteral("(() => { const spec = ") + spec + QStringLiteral("; const wantClick = ") + click
           + QStringLiteral(R"js(;
  if (spec.indexOf('text:') === 0) {
    const re = new RegExp(spec.slice(5), 'i');
    const el = [...document.querySelectorAll('button, a, [role=button]')].find(n => re.test(((n.innerText || n.getAttribute('aria-label') || '') + '')));
    if (el) { try { el.click(); } catch (e) {} }
  } else if (spec) {
    const el = document.querySelector(spec);
    if (el) { try { el.click(); } catch (e) {} }
  }
  const videos = [...document.querySelectorAll('video')];
  const score = (v) => (v.mediaKeys ? 1000000 : 0) + (v.muted ? 0 : 10000) + (v.videoWidth || 0);
  let video = null, best = -1;
  for (const v of videos) { const s = score(v); if (s > best) { best = s; video = v; } }
  if (!video) return JSON.stringify({x:0,y:0,click:false});
  try { video.focus(); } catch (e) {}
  const r = video.getBoundingClientRect();
  const x = Math.round(r.left + Math.max(r.width, 2) / 2);
  const y = Math.round(r.top + Math.max(r.height, 2) / 2);
  if (wantClick) { try { video.click(); } catch (e) {} }
  return JSON.stringify({x:x, y:y, click:wantClick});
})())js");
}

QString mirrorTransportScript(const QString &action)
{
    return QStringLiteral("(() => { const act = ") + jsString(action)
           + QStringLiteral(R"js(;
  const videos = [...document.querySelectorAll('video')];
  const score = (v) => (v.mediaKeys ? 1000000 : 0) + (v.muted ? 0 : 10000) + (v.videoWidth || 0);
  let video = null, best = -1;
  for (const v of videos) { const s = score(v); if (s > best) { best = s; video = v; } }
  if (!video) return 'missing';
  try { video.focus(); } catch (e) {}
  if (act === 'ok' || act === 'play') {
    if (video.paused) { const p = video.play(); if (p && p.catch) p.catch(() => {}); }
    else video.pause();
  } else if (act === 'left') {
    video.currentTime = Math.max(0, (video.currentTime || 0) - 10);
  } else if (act === 'right') {
    video.currentTime = (video.currentTime || 0) + 10;
  }
  return 'ok';
})())js");
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
    QStringList hints = adapter.playPaths;
    if (hints.isEmpty()) {
        hints = {QStringLiteral("/watch"), QStringLiteral("/play"), QStringLiteral("/player"), QStringLiteral("/video"),
                 QStringLiteral("/v/")};
    }
    for (const QString &hint : hints) {
        if (!hint.isEmpty() && path.contains(hint.toLower()))
            return trimmed;
    }
    return QString();
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
            if (name.isEmpty() || href.isEmpty() || href.startsWith(QStringLiteral("zappe://")) || titleRejected(adapter, name))
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

bool mirrorPageHasTitles(const MirrorPage &page)
{
    for (const ShelfData &shelf : page.shelves) {
        if (shelf.id.endsWith(QStringLiteral(":profiles")) || shelf.status != QLatin1String("ok"))
            continue;
        if (!shelf.rows.isEmpty())
            return true;
    }
    return false;
}

bool mirrorPageUsable(const MirrorPage &page, int minTitles)
{
    if (!page.ok || page.needsLogin || minTitles < 1)
        return false;
    int count = 0;
    for (const ShelfData &shelf : page.shelves) {
        if (shelf.id.endsWith(QStringLiteral(":profiles")) || shelf.status != QLatin1String("ok"))
            continue;
        for (const RowData &row : shelf.rows) {
            if (row.title.isEmpty() || row.href.isEmpty() || row.href.startsWith(QStringLiteral("zappe://")))
                continue;
            if (!row.artworkSpec.startsWith(QStringLiteral("http")))
                continue;
            ++count;
        }
    }
    return count >= minTitles;
}

MirrorPage parseMirrorFeeds(const QVector<NetworkSample> &samples, const MirrorAdapter &adapter)
{
    if (adapter.feeds.isEmpty())
        return {};
    QVector<FeedItem> items;
    QSet<QString> seen;
    for (const NetworkSample &sample : samples) {
        const QJsonDocument doc = QJsonDocument::fromJson(sample.body);
        if (doc.isNull())
            continue;
        const QJsonValue root = doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
        for (const MirrorFeed &feed : adapter.feeds) {
            if (!feedMatches(feed, sample.url))
                continue;
            QVector<FeedItem> found;
            walkFeed(root, QString(), adapter, feed, found, 0);
            for (const FeedItem &item : found) {
                if (item.href.isEmpty() || seen.contains(item.href))
                    continue;
                seen.insert(item.href);
                items.append(item);
            }
        }
    }
    struct Bucket {
        QString title;
        QJsonArray rows;
    };
    QVector<Bucket> buckets;
    for (const FeedItem &item : items) {
        int index = -1;
        for (int i = 0; i < buckets.size(); ++i) {
            if (buckets.at(i).title == item.shelf) {
                index = i;
                break;
            }
        }
        if (index < 0) {
            if (buckets.size() >= adapter.limitRows)
                continue;
            buckets.append(Bucket{item.shelf, QJsonArray()});
            index = buckets.size() - 1;
        }
        if (buckets.at(index).rows.size() >= adapter.limitCards)
            continue;
        QJsonObject row;
        row.insert(QStringLiteral("title"), item.title);
        row.insert(QStringLiteral("href"), item.href);
        row.insert(QStringLiteral("artwork"), item.artwork);
        row.insert(QStringLiteral("progress"), 0);
        buckets[index].rows.append(row);
    }
    QJsonArray shelves;
    for (const Bucket &bucket : buckets) {
        QJsonObject shelf;
        shelf.insert(QStringLiteral("title"), bucket.title);
        shelf.insert(QStringLiteral("context"), bucket.title);
        shelf.insert(QStringLiteral("rows"), bucket.rows);
        shelves.append(shelf);
    }
    QJsonObject root;
    root.insert(QStringLiteral("needsLogin"), false);
    root.insert(QStringLiteral("profiles"), QJsonArray());
    root.insert(QStringLiteral("shelves"), shelves);
    return parseMirrorPage(QJsonDocument(root).toJson(QJsonDocument::Compact), adapter);
}

QString mirrorDomSnapshotScript()
{
    return QStringLiteral(R"js((() => {
  const skip = {script:1, style:1, svg:1, noscript:1, path:1};
  const nodes = [];
  const walk = (el, depth) => {
    if (!el || depth > 5 || nodes.length >= 60 || el.nodeType !== 1) return;
    const tag = (el.tagName || '').toLowerCase();
    if (skip[tag]) return;
    const cls = ((el.className && el.className.baseVal) || el.className || '').toString().replace(/\s+/g, ' ').trim().slice(0, 80);
    const item = {t: tag};
    if (el.id) item.id = String(el.id).slice(0, 60);
    if (cls) item.c = cls;
    const href = el.getAttribute && el.getAttribute('href');
    if (href) item.h = String(href).slice(0, 160);
    const al = el.getAttribute && (el.getAttribute('aria-label') || el.getAttribute('alt'));
    if (al) item.a = String(al).replace(/\s+/g, ' ').trim().slice(0, 80);
    nodes.push(item);
    const kids = el.children || [];
    for (let i = 0; i < kids.length && nodes.length < 60; i++) walk(kids[i], depth + 1);
  };
  walk(document.body, 0);
  return JSON.stringify({url: location.href, title: document.title, nodes: nodes});
})())js");
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
    m_adapters = loadMirrorAdapters(dataDir);
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
    connect(m_cdp, &CdpClient::networkResponse, this, &MirrorSession::onNetworkResponse);
    connect(m_cdp, &CdpClient::networkFinished, this, &MirrorSession::onNetworkFinished);
    connect(m_cdp, &CdpClient::responseBody, this, &MirrorSession::onResponseBody);
    connect(m_cdp, &CdpClient::screenshotReady, this, &MirrorSession::onScreenshot);
    connect(m_cdp, &CdpClient::failed, this, [this](const QString &message) {
        std::fprintf(stderr, "zappe: espelho cdp falhou: %s\n", qPrintable(message));
        if (m_job == JobLearnDom || m_job == JobLearnShot)
            return;
        const bool down = !m_cdp || !m_cdp->connected();
        if (m_job == JobPlay)
            emit failed(message);
        if (down) {
            if (m_job != JobPlay)
                m_busy = false;
            scheduleAttach();
            return;
        }
        if (m_job == JobPlay) {
            m_busy = false;
            return;
        }
        if (!m_current.isEmpty() && (m_job == JobExtract || m_job == JobSearch))
            showError(m_current, message);
        ++m_epoch;
        m_busy = false;
        m_job = JobNone;
        QTimer::singleShot(400, this, &MirrorSession::pump);
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
    m_attachTries = 0;
    scheduleAttach();
}

void MirrorSession::scheduleAttach()
{
    if (!m_cdp || !m_chrome || m_cdp->connected())
        return;
    if (m_attachTries >= 12) {
        std::fprintf(stderr, "zappe: espelho sem CDP na porta %d\n", m_chrome->port());
        return;
    }
    const int attempt = m_attachTries++;
    int delay = 0;
    if (attempt > 0) {
        delay = 250;
        for (int i = 1; i < attempt && delay < 2000; ++i)
            delay *= 2;
        delay = std::min(delay, 2000);
    }
    QTimer::singleShot(delay, this, [this] {
        if (!m_cdp || !m_chrome || m_cdp->connected())
            return;
        std::fprintf(stderr, "zappe: espelho attach porta %d tentativa %d\n", m_chrome->port(), m_attachTries);
        m_cdp->attach(m_chrome->port());
    });
}

void MirrorSession::onAttached()
{
    m_attachTries = 0;
    if (m_cdp)
        m_cdp->enableNetwork();
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
    armWatch(serviceId);
}

void MirrorSession::armWatch(const QString &serviceId)
{
    const int token = m_loadToken.value(serviceId) + 1;
    m_loadToken.insert(serviceId, token);
    QTimer::singleShot(mirrorLoadTimeoutMs(), this, [this, serviceId, token] {
        if (m_loadToken.value(serviceId) != token || m_player)
            return;
        if (m_pages.contains(serviceId) && !m_pages.value(serviceId).isEmpty())
            return;
        std::fprintf(stderr, "zappe: espelho timeout %s depois de %d ms\n", qPrintable(serviceId), mirrorLoadTimeoutMs());
        showError(serviceId, QString());
        if (m_player)
            return;
        const qint64 busyFor = QDateTime::currentMSecsSinceEpoch() - m_busySince;
        if (m_busy && (m_current == serviceId || !m_cdp || !m_cdp->connected() || busyFor >= mirrorLoadTimeoutMs())) {
            std::fprintf(stderr, "zappe: espelho abortando job %s preso há %lld ms\n", qPrintable(m_current),
                         static_cast<long long>(busyFor));
            ++m_epoch;
            m_busy = false;
            m_job = JobNone;
        }
        if (!m_cdp || !m_cdp->connected())
            scheduleAttach();
        else if (!m_busy)
            QTimer::singleShot(400, this, &MirrorSession::pump);
    });
}

void MirrorSession::showError(const QString &serviceId, const QString &detail)
{
    const MirrorAdapter *adapter = findMirrorAdapter(m_adapters, serviceId);
    if (!m_guide || !adapter)
        return;
    m_loadToken.insert(serviceId, m_loadToken.value(serviceId) + 1);
    QString message = QStringLiteral("O Chrome não abriu esta página.");
    if (detail.contains(QStringLiteral("sem aba"), Qt::CaseInsensitive))
        message = QStringLiteral("O Chrome não abriu uma aba.");
    else if (detail.contains(QStringLiteral("sem conexão"), Qt::CaseInsensitive)
             || detail.contains(QStringLiteral("Connection refused"), Qt::CaseInsensitive)
             || detail.contains(QStringLiteral("desconectou"), Qt::CaseInsensitive))
        message = QStringLiteral("O Chrome não respondeu.");
    message += QStringLiteral("\nTentar de novo");
    ShelfData shelf;
    shelf.id = QStringLiteral("mirror:%1:error").arg(serviceId);
    shelf.skillId = QStringLiteral("mirror:") + serviceId;
    shelf.title = adapter->name;
    shelf.status = QStringLiteral("error");
    shelf.message = message;
    std::fprintf(stderr, "zappe: espelho erro %s: %s\n", qPrintable(serviceId),
                 qPrintable(detail.isEmpty() ? message : detail));
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

void MirrorSession::play(const QString &serviceId, const QString &href)
{
    ++m_epoch;
    m_player = true;
    m_busy = true;
    m_job = JobPlay;
    m_playerService = serviceId;
    m_pendingUrl = href;
    m_queue.clear();
    m_samples.clear();
    m_inflight.clear();
    if (m_chrome)
        m_chrome->present(m_dataDir);
}

void MirrorSession::leavePlayer()
{
    const bool was = m_player;
    ++m_epoch;
    m_player = false;
    m_busy = false;
    m_job = JobNone;
    m_pendingUrl.clear();
    if (was && m_cdp && m_cdp->connected())
        m_cdp->closeBrowser();
    if (m_chrome)
        m_chrome->relaunchHidden();
    if (!m_current.isEmpty())
        m_queue.prepend(m_current);
}

void MirrorSession::dispatchAction(const QString &action)
{
    if (!m_cdp || !m_cdp->connected())
        return;
    const QString name = action.trimmed().toLower();
    if (name == QLatin1String("back"))
        return;
    const MirrorAdapter *adapter = findMirrorAdapter(m_adapters, m_playerService);
    const bool scriptSeek = adapter && adapter->playerSeek == QLatin1String("script");
    const bool scriptOk = adapter && adapter->playerOk == QLatin1String("script");
    const bool seek = name == QLatin1String("left") || name == QLatin1String("right");
    const bool toggle = name == QLatin1String("ok") || name == QLatin1String("play");
    if ((seek && scriptSeek) || (toggle && scriptOk)) {
        m_cdp->evaluate(mirrorTransportScript(name));
        return;
    }
    m_cdp->dispatchAction(name == QLatin1String("ok") ? QStringLiteral("play") : name);
}

void MirrorSession::focusPlayer()
{
    if (!m_cdp || !m_cdp->connected())
        return;
    const MirrorAdapter *adapter = findMirrorAdapter(m_adapters, m_playerService);
    if (!adapter) {
        m_cdp->evaluate(mirrorFocusPlayerScript(MirrorAdapter()));
        return;
    }
    m_cdp->evaluate(mirrorFocusPlayerScript(*adapter));
}

void MirrorSession::begin(const QString &serviceId, const QString &url, int job)
{
    if (m_player || url.isEmpty())
        return;
    if (!m_cdp || !m_cdp->connected()) {
        m_queue.prepend(serviceId);
        return;
    }
    ++m_epoch;
    m_current = serviceId;
    m_navUrl = url;
    if (job == JobSearch)
        m_extraLeft.clear();
    m_job = job;
    m_busy = true;
    m_busySince = QDateTime::currentMSecsSinceEpoch();
    m_learnPosted = false;
    m_samples.clear();
    m_inflight.clear();
    std::fprintf(stderr, "zappe: espelho %s -> %s\n", qPrintable(serviceId), qPrintable(url));
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
    m_extraLeft = adapter->extraUrls;
    m_mergePage = false;
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
    const int epoch = m_epoch;
    const QString serviceId = m_current;
    const int job = m_job;
    QTimer::singleShot(1800, this, [this, epoch, serviceId, job] { settle(epoch, serviceId, job, 0); });
}

void MirrorSession::settle(int epoch, const QString &serviceId, int job, int waited)
{
    if (m_player || epoch != m_epoch || m_current != serviceId || m_job != job)
        return;
    if (!m_inflight.isEmpty() && waited < 2000) {
        QTimer::singleShot(400, this, [this, epoch, serviceId, job, waited] { settle(epoch, serviceId, job, waited + 400); });
        return;
    }
    considerExtract();
}

void MirrorSession::considerExtract()
{
    if (m_player || (m_job != JobExtract && m_job != JobSearch) || !m_cdp)
        return;
    const MirrorAdapter *adapter = findMirrorAdapter(m_adapters, m_current);
    if (!adapter) {
        m_busy = false;
        m_job = JobNone;
        QTimer::singleShot(400, this, &MirrorSession::pump);
        return;
    }
    const MirrorPage network = parseMirrorFeeds(m_samples, *adapter);
    if (mirrorPageHasTitles(network)) {
        acceptPage(network);
        return;
    }
    m_cdp->evaluate(mirrorExtractScript(*adapter));
}

void tagHistoryShelves(MirrorPage &page, const QString &url, const QString &serviceId)
{
    if (!url.contains(QStringLiteral("/feed/history")))
        return;
    int index = 0;
    for (ShelfData &shelf : page.shelves) {
        if (shelf.id.contains(QStringLiteral(":profiles")))
            continue;
        shelf.id = QStringLiteral("mirror:%1:continue").arg(serviceId);
        if (index > 0)
            shelf.id += QStringLiteral("-") + QString::number(index);
        for (RowData &row : shelf.rows)
            row.wide = true;
        ++index;
    }
}

void MirrorSession::acceptPage(const MirrorPage &page)
{
    const bool extract = m_job == JobExtract;
    const MirrorAdapter *adapter = findMirrorAdapter(m_adapters, m_current);
    if (page.needsLogin && adapter && !m_askedLogin) {
        m_askedLogin = true;
        emit needsLogin(adapter->id);
    }
    MirrorPage tagged = page;
    tagHistoryShelves(tagged, m_navUrl, m_current);
    applyPage(tagged);
    if (extract && !tagged.needsLogin && !m_extraLeft.isEmpty() && !m_player && m_cdp && m_cdp->connected()) {
        const QString next = m_extraLeft.takeFirst();
        m_mergePage = true;
        begin(m_current, next, JobExtract);
        return;
    }
    m_mergePage = false;
    m_busy = false;
    m_job = JobNone;
    QTimer::singleShot(500, this, &MirrorSession::pump);
}

void MirrorSession::finishEmpty()
{
    m_busy = false;
    m_job = JobNone;
    MirrorPage empty;
    empty.ok = true;
    applyPage(empty);
    QTimer::singleShot(500, this, &MirrorSession::pump);
}

void MirrorSession::beginRepair()
{
    const QString serviceId = m_current;
    const MirrorAdapter *adapter = findMirrorAdapter(m_adapters, serviceId);
    if (!adapter || m_player || !m_cdp) {
        finishEmpty();
        return;
    }
    if (!m_rolled.contains(serviceId)) {
        m_rolled.insert(serviceId);
        if (rollbackLearnedAdapter(m_dataDir, serviceId)) {
            m_adapters = loadMirrorAdapters(m_dataDir);
            adapter = findMirrorAdapter(m_adapters, serviceId);
            std::fprintf(stderr, "zappe: adaptador %s restaurado do histórico\n", qPrintable(serviceId));
            if (adapter) {
                const MirrorPage network = parseMirrorFeeds(m_samples, *adapter);
                if (mirrorPageHasTitles(network)) {
                    acceptPage(network);
                    return;
                }
                m_job = JobExtract;
                m_busy = true;
                m_cdp->evaluate(mirrorExtractScript(*adapter));
                return;
            }
        }
    }
    if (m_learned.contains(serviceId)) {
        finishEmpty();
        return;
    }
    m_learned.insert(serviceId);
    const LlmConfig config = loadLlmConfig(m_dataDir);
    if (!llmEnabled(config)) {
        std::fprintf(stderr, "zappe: aprendizado desligado para %s (sem URL em llm.json)\n", qPrintable(serviceId));
        finishEmpty();
        return;
    }
    m_job = JobLearnDom;
    m_busy = true;
    m_domSnap.clear();
    m_learnPosted = false;
    m_cdp->evaluate(mirrorDomSnapshotScript());
}

void MirrorSession::postLearn(const QByteArray &png)
{
    if (m_learnPosted || m_job != JobLearnShot)
        return;
    m_learnPosted = true;
    const MirrorAdapter *adapter = findMirrorAdapter(m_adapters, m_current);
    const LlmConfig config = loadLlmConfig(m_dataDir);
    if (!adapter || !llmEnabled(config)) {
        finishEmpty();
        return;
    }
    const QString serviceId = adapter->id;
    const QByteArray body = llmRepairBody(*adapter, m_domSnap, m_samples, png, config.model);
    QNetworkRequest request{QUrl(config.url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QByteArray authorization = llmAuthorization(config);
    if (!authorization.isEmpty())
        request.setRawHeader("Authorization", authorization);
    QNetworkReply *reply = m_http->post(request, body);
    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(25000);
    connect(reply, &QNetworkReply::finished, this, [this, reply, timer, serviceId] {
        timer->stop();
        timer->deleteLater();
        const QByteArray payload = reply->readAll();
        const bool httpOk = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        if (m_current != serviceId || (m_job != JobLearnShot && m_job != JobLearnDom))
            return;
        const MirrorAdapter *current = findMirrorAdapter(m_adapters, serviceId);
        const bool saved = httpOk && current && applyLearnResponse(m_dataDir, *current, m_samples, payload);
        if (saved) {
            m_adapters = loadMirrorAdapters(m_dataDir);
            const MirrorAdapter *fresh = findMirrorAdapter(m_adapters, serviceId);
            if (fresh) {
                const MirrorPage page = parseMirrorFeeds(m_samples, *fresh);
                if (mirrorPageHasTitles(page)) {
                    std::fprintf(stderr, "zappe: adaptador %s atualizado\n", qPrintable(serviceId));
                    acceptPage(page);
                    return;
                }
            }
        }
        std::fprintf(stderr, "zappe: adaptador %s não foi salvo\n", qPrintable(serviceId));
        finishEmpty();
    });
}

void MirrorSession::onNetworkResponse(const QString &requestId, const QString &url, const QString &mimeType)
{
    if (requestId.isEmpty() || m_player)
        return;
    if (m_job != JobExtract && m_job != JobSearch && m_job != JobLearnDom && m_job != JobLearnShot)
        return;
    if (!wantNetworkBody(m_adapters, m_current, url, mimeType))
        return;
    const bool feedHit = matchesAdapterFeed(m_adapters, m_current, url);
    if (m_samples.size() + m_inflight.size() >= 8) {
        if (!feedHit)
            return;
        evictNonFeed(m_samples, m_adapters, m_current);
        if (m_samples.size() + m_inflight.size() >= 8) {
            QString drop;
            for (auto it = m_inflight.cbegin(); it != m_inflight.cend(); ++it) {
                if (!matchesAdapterFeed(m_adapters, m_current, it.value())) {
                    drop = it.key();
                    break;
                }
            }
            if (drop.isEmpty())
                return;
            m_inflight.remove(drop);
        }
    }
    m_inflight.insert(requestId, url);
}

void MirrorSession::onNetworkFinished(const QString &requestId)
{
    if (!m_inflight.contains(requestId) || !m_cdp)
        return;
    m_cdp->getResponseBody(requestId);
}

void MirrorSession::onResponseBody(const QString &requestId, const QByteArray &body, bool ok)
{
    const QString url = m_inflight.take(requestId);
    if (!ok || url.isEmpty() || body.isEmpty())
        return;
    if (m_samples.size() >= 8) {
        if (!matchesAdapterFeed(m_adapters, m_current, url))
            return;
        evictNonFeed(m_samples, m_adapters, m_current);
        if (m_samples.size() >= 8)
            return;
    }
    const QByteArray trimmed = body.trimmed();
    if (!trimmed.startsWith('{') && !trimmed.startsWith('['))
        return;
    NetworkSample sample;
    sample.url = url;
    sample.body = trimmed.left(48 * 1024);
    m_samples.append(sample);
}

void MirrorSession::onScreenshot(const QByteArray &png, bool ok)
{
    if (m_job != JobLearnShot)
        return;
    postLearn(ok ? png : QByteArray());
}

void MirrorSession::onEvaluated(const QString &value, bool ok)
{
    if (m_job == JobPlay) {
        if (!ok || !m_cdp)
            return;
        const QJsonObject obj = QJsonDocument::fromJson(value.toUtf8()).object();
        if (obj.value(QStringLiteral("click")).toBool()) {
            const int x = obj.value(QStringLiteral("x")).toInt();
            const int y = obj.value(QStringLiteral("y")).toInt();
            if (x > 0 && y > 0)
                m_cdp->dispatchMouseClick(x, y);
        }
        return;
    }
    if (m_job == JobProfile) {
        m_busy = false;
        m_job = JobNone;
        if (!m_current.isEmpty())
            refresh(m_current);
        return;
    }
    if (m_job == JobLearnDom) {
        if (ok)
            m_domSnap = value.left(12000);
        m_job = JobLearnShot;
        m_learnPosted = false;
        const QString serviceId = m_current;
        QTimer::singleShot(4000, this, [this, serviceId] {
            if (m_job == JobLearnShot && m_current == serviceId)
                postLearn(QByteArray());
        });
        if (m_cdp)
            m_cdp->captureScreenshot();
        return;
    }
    if (m_job != JobExtract && m_job != JobSearch)
        return;
    const bool search = m_job == JobSearch;
    const MirrorAdapter *adapter = findMirrorAdapter(m_adapters, m_current);
    if (search) {
        if (!adapter || !ok) {
            m_busy = false;
            m_job = JobNone;
            QTimer::singleShot(600, this, &MirrorSession::pump);
            return;
        }
        acceptPage(parseMirrorPage(value.toUtf8(), *adapter));
        return;
    }
    if (!adapter || !ok) {
        beginRepair();
        return;
    }
    const MirrorPage page = parseMirrorPage(value.toUtf8(), *adapter);
    if (page.needsLogin || mirrorPageHasTitles(page)) {
        acceptPage(page);
        return;
    }
    beginRepair();
}

void MirrorSession::applyPage(const MirrorPage &page)
{
    if (!m_guide || m_current.isEmpty())
        return;
    const MirrorAdapter *adapter = findMirrorAdapter(m_adapters, m_current);
    if (!adapter)
        return;
    m_loadToken.insert(adapter->id, m_loadToken.value(adapter->id) + 1);
    QVector<ShelfData> shelves = page.shelves;
    if (m_mergePage) {
        QVector<ShelfData> prior;
        for (const ShelfData &shelf : m_pages.value(adapter->id)) {
            if (shelf.status == QLatin1String("ok") && !shelf.rows.isEmpty())
                prior.append(shelf);
        }
        prior += shelves;
        shelves = prior;
    }
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
