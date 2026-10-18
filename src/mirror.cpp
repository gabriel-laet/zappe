#include "mirror.h"

#include "cdp.h"
#include "chrome.h"
#include "guide.h"
#include "learn.h"
#include "posters.h"
#include "util.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QProcess>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cstdio>

namespace zappe {

void touchCoverFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite))
        return;
    file.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
}

namespace {

enum Job {
    JobNone = 0,
    JobExtract = 1,
    JobSearch = 2,
    JobPlay = 3,
    JobProfile = 4,
    JobLearnDom = 5,
    JobLearnShot = 6,
    JobCovers = 7
};

int shelfTitleCount(const QVector<ShelfData> &shelves)
{
    int count = 0;
    for (const ShelfData &shelf : shelves) {
        if (shelf.status == QLatin1String("ok"))
            count += shelf.rows.size();
    }
    return count;
}

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
        QString text = value.toString().trimmed();
        if (text.startsWith(QStringLiteral("//")))
            text.prepend(QStringLiteral("https:"));
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
    // url/src antes de uri/images: a HBO manda os dois e o uri sem cookie responde 404.
    const QStringList keys = {QStringLiteral("thumbnails"), QStringLiteral("url"),       QStringLiteral("src"),
                              QStringLiteral("packshot"),   QStringLiteral("thumbnail"), QStringLiteral("poster"),
                              QStringLiteral("artwork"),    QStringLiteral("boxart"),    QStringLiteral("storyArt"),
                              QStringLiteral("cover"),      QStringLiteral("image"),     QStringLiteral("images"),
                              QStringLiteral("uri")};
    for (const QString &key : keys) {
        if (!obj.contains(key))
            continue;
        const QString art = jsonArt(obj.value(key), depth + 1);
        if (!art.isEmpty())
            return art;
    }
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (!it.value().isString())
            continue;
        const QString art = jsonArt(it.value(), depth + 1);
        const QString lower = art.toLower();
        if (lower.contains(QStringLiteral("media-amazon.com")) || lower.contains(QStringLiteral("mzstatic.com")))
            return art;
    }
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

bool imageResponse(const QString &url, const QString &mime)
{
    const QString mimeLower = mime.toLower();
    if (mimeLower.startsWith(QStringLiteral("image/")))
        return true;
    const QString urlLower = url.toLower();
    return urlLower.contains(QStringLiteral(".jpg")) || urlLower.contains(QStringLiteral(".jpeg"))
           || urlLower.contains(QStringLiteral(".png")) || urlLower.contains(QStringLiteral(".webp"))
           || urlLower.contains(QStringLiteral(".avif")) || urlLower.contains(QStringLiteral(".gif"));
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
                touchCoverFile(path);
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

int mirrorCoverUnusedMaxAgeSecs()
{
    return 14 * 24 * 60 * 60;
}

int mirrorRefreshMs()
{
    // 45 min. ZAPPE_MIRROR_REFRESH_MIN fica entre 30 e 60.
    int minutes = 45;
    bool ok = false;
    const int env = qEnvironmentVariableIntValue("ZAPPE_MIRROR_REFRESH_MIN", &ok);
    if (ok)
        minutes = std::clamp(env, 30, 60);
    return minutes * 60 * 1000;
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
  const pickSrc = (value) => {
    if (!value) return '';
    const text = String(value).trim();
    if (!text || text.indexOf('data:') === 0 || text.indexOf('blob:') === 0) return '';
    let best = '', bestW = -1;
    for (const part of text.split(',')) {
      const bits = part.trim().split(/\s+/);
      const url = bits[0] || '';
      if (url.indexOf('http') !== 0 && url.indexOf('//') !== 0) continue;
      const w = bits[1] && /[\d.]+w$/.test(bits[1]) ? parseInt(bits[1], 10) : 1;
      if (w >= bestW) { bestW = w; best = url; }
    }
    return best;
  };
  const bgUrl = (el) => {
    if (!el) return '';
    const raw = ((el.style && el.style.backgroundImage) || '') + ' ' + ((el.getAttribute && el.getAttribute('style')) || '');
    const match = /url\((['"]?)((?:https?:)?\/\/[^'")]+)\1\)/.exec(raw);
    return match ? match[2] : '';
  };
  const placeholder = (url) => {
    const u = String(url || '').toLowerCase();
    if (u.indexOf('.gif') < 0) return false;
    return u.indexOf('1x1') >= 0 || u.indexOf('pixel') >= 0 || u.indexOf('spacer') >= 0;
  };
  const httpUrl = (value) => {
    let url = pickSrc(value);
    if (!url) return '';
    if (url.indexOf('//') === 0) url = 'https:' + url;
    if (placeholder(url)) return '';
    return url.indexOf('http') === 0 ? url : '';
  };
  const collectArt = (root, nodes, depth) => {
    if (!root || depth > 3 || nodes.length >= 8) return;
    const queue = [root];
    let seen = 0;
    while (queue.length && nodes.length < 8 && seen < 40) {
      const el = queue.shift();
      if (!el) continue;
      seen++;
      try {
        const tag = String(el.tagName || '').toLowerCase();
        const style = (el.getAttribute && (el.getAttribute('style') || '')) || '';
        if (tag === 'img' || tag === 'source' || style.indexOf('background') >= 0) nodes.push(el);
      } catch (e) {}
      try { if (el.shadowRoot) collectArt(el.shadowRoot, nodes, depth + 1); } catch (e) {}
      try {
        const kids = el.children || [];
        for (let i = 0; i < kids.length && queue.length < 40; i++) queue.push(kids[i]);
      } catch (e) {}
    }
  };
  const artIn = (root) => {
    if (!root) return '';
    const nodes = [];
    collectArt(root, nodes, 0);
    nodes.push(root);
    for (const el of nodes) {
      if (!el) continue;
      const loaded = httpUrl(el.currentSrc) || httpUrl(el.src) || httpUrl(el.getAttribute && el.getAttribute('src'));
      if (loaded) return loaded;
    }
    for (const el of nodes) {
      if (!el) continue;
      const vals = [];
      if (el.getAttribute) {
        vals.push(el.getAttribute('srcset') || '');
        vals.push(el.getAttribute('data-srcset') || '');
        vals.push(el.getAttribute('data-src') || '');
        vals.push(el.getAttribute('data-lazy-src') || '');
        vals.push(el.getAttribute('data-original') || '');
      }
      vals.push(bgUrl(el));
      for (const value of vals) {
        const url = httpUrl(value);
        if (url) return url;
      }
    }
    return '';
  };
  const artOf = (root) => {
    const own = artIn(root);
    if (own) return own;
    const tag = root && root.tagName ? String(root.tagName).toLowerCase() : '';
    if (tag === 'a') return '';
    const parent = root && root.parentElement;
    if (!parent || !parent.children) return '';
    for (const kid of parent.children) {
      if (!kid || kid === root) continue;
      if (kid.querySelector && kid.querySelector('a[href]')) continue;
      const url = artIn(kid);
      if (url) return url;
    }
    return '';
  };
  const synopsisOf = (root, title) => {
    if (!root || !root.querySelectorAll) return '';
    let text = '';
    for (const el of root.querySelectorAll('p, span, div')) {
      const cls = ((el.className && el.className.toString) ? el.className.toString() : '')
        + ' ' + ((el.getAttribute && (el.getAttribute('data-testid') || el.getAttribute('data-uia') || '')) || '');
      if (!/synopsis|description|overview|plot/i.test(cls)) continue;
      text = label(el);
      if (text && text !== title) break;
      text = '';
    }
    text = (text || '').replace(/\s+/g, ' ').trim();
    if (!text || text === title) return '';
    if (text.length > 280) text = text.slice(0, 277) + '…';
    return text;
  };
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
    let profiles = [];
    if (cfg.profile) {
      try { profiles = [...document.querySelectorAll(cfg.profile)].map(label).filter(Boolean); }
      catch (e) { profiles = []; }
    }
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
        try {
          const link = (card.matches && card.matches('a[href]')) ? card : (card.querySelector(cfg.link) || card.querySelector('a[href]'));
          const img = card.querySelector(cfg.image);
          const href = abs(link && link.getAttribute('href'));
          const name = label(link) || label(img) || label(card);
          if (!href || !name || seen.has(href)) continue;
          seen.add(href);
          rows.push({title: name, href: href, artwork: artOf(card), synopsis: synopsisOf(card, name), progress: progressOf(card.querySelector(cfg.progress))});
        } catch (e) {}
      }
      if (!rows.length) continue;
      const key = (title || 'row') + ':' + rows[0].href;
      if (seenRow.has(key)) continue;
      seenRow.add(key);
      shelves.push({title: title, context: context, rows: rows});
    }
    if (!shelves.length && cfg.link) {
      const rows = [];
      const seen = new Set();
      for (const link of document.querySelectorAll(cfg.link)) {
        if (rows.length >= cfg.maxCards) break;
        if (!link || (link.matches && !link.matches('a[href]'))) continue;
        const img = link.querySelector(cfg.image);
        const href = abs(link.getAttribute('href'));
        const name = label(link) || label(img);
        if (!href || !name || seen.has(href)) continue;
        seen.add(href);
        let art = '';
        try { art = artOf(link); } catch (e) { art = ''; }
        rows.push({title: name, href: href, artwork: art, synopsis: synopsisOf(link, name), progress: progressOf(link.querySelector(cfg.progress))});
      }
      if (rows.length)
        shelves.push({title: '', context: 'flatLinks', rows: rows});
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
        QString id = match.hasMatch() ? match.captured(1) : QString();
        if (id.isEmpty()) {
            const QString jbv = QUrlQuery(url).queryItemValue(QStringLiteral("jbv"));
            if (QRegularExpression(QStringLiteral("^\\d{4,12}$")).match(jbv).hasMatch())
                id = jbv;
        }
        if (id.isEmpty())
            return {};
        return QStringLiteral("https://www.netflix.com/watch/%1").arg(id);
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

bool mirrorUrlIsPlayback(const QString &url)
{
    const QUrl parsed(url.trimmed());
    if (!parsed.isValid())
        return false;
    const QString path = parsed.path().toLower();
    const QString host = parsed.host().toLower();
    const QString query = parsed.query().toLower();
    if (path.contains(QStringLiteral("/watch")) || path.contains(QStringLiteral("/player")))
        return true;
    if (host.contains(QStringLiteral("youtube")) && query.contains(QStringLiteral("v=")))
        return true;
    return false;
}

QString mirrorRowService(const QString &service, const QString &href)
{
    const QVector<MirrorAdapter> adapters = loadMirrorAdapters();
    const MirrorAdapter *adapter = findMirrorAdapter(adapters, service);
    if (!adapter || mirrorIsPlayable(*adapter, href))
        return {};
    return adapter->id;
}

bool mirrorKeepRicherPage(int previousTitles, int incomingTitles)
{
    return previousTitles >= 4 && incomingTitles * 2 < previousTitles;
}

bool mirrorHoldLoading(int previousTitles, int incomingTitles)
{
    return previousTitles <= 0 && incomingTitles <= 0;
}

QString mirrorCoverFetchScript(const QStringList &urls)
{
    QStringList quoted;
    for (const QString &url : urls)
        quoted.append(jsString(url));
    return QStringLiteral("(() => { const urls = [") + quoted.join(QLatin1Char(','))
           + QStringLiteral(R"js(];
  const b64 = (buf) => {
    const bytes = new Uint8Array(buf);
    let s = '';
    const step = 8192;
    for (let i = 0; i < bytes.length; i += step) {
      const slice = bytes.subarray(i, Math.min(i + step, bytes.length));
      s += String.fromCharCode.apply(null, slice);
    }
    return btoa(s);
  };
  const one = async (url) => {
    const ctrl = new AbortController();
    const timer = setTimeout(() => ctrl.abort(), 4000);
    try {
      const res = await fetch(url, {credentials: 'include', signal: ctrl.signal});
      if (!res.ok) return {url: url, ok: false, err: 'http ' + res.status};
      const buf = await res.arrayBuffer();
      if (buf.byteLength < 32 || buf.byteLength > 500000) return {url: url, ok: false, err: 'tamanho ' + buf.byteLength};
      return {url: url, ok: true, b64: b64(buf)};
    } catch (e) {
      return {url: url, ok: false, err: String((e && e.message) || e)};
    } finally {
      clearTimeout(timer);
    }
  };
  return Promise.all(urls.map(one)).then((rows) => JSON.stringify(rows));
})())js");
}

QString coverMatchKey(const QString &url)
{
    const QUrl parsed(url);
    if (!parsed.isValid() || parsed.host().isEmpty())
        return url.left(200);
    return parsed.host().toLower() + parsed.path();
}

bool coverTokenIsGeneric(const QString &token)
{
    const QString lower = token.toLower();
    const QStringList generic = {QStringLiteral("maxresdefault"), QStringLiteral("hqdefault"),
                                 QStringLiteral("mqdefault"),     QStringLiteral("sddefault"),
                                 QStringLiteral("thumbnail"),     QStringLiteral("packshot"),
                                 QStringLiteral("640x360bb"),     QStringLiteral("background")};
    return generic.contains(lower);
}

bool coverFileIsDistinctive(const QString &file)
{
    const QString name = file.trimmed().toLower();
    if (name.size() < 12 || coverTokenIsGeneric(name.section(QLatin1Char('.'), 0, 0)))
        return false;
    const QStringList generic = {QStringLiteral("640x360bb.jpg"), QStringLiteral("hqdefault.jpg"),
                                 QStringLiteral("mqdefault.jpg"), QStringLiteral("sddefault.jpg"),
                                 QStringLiteral("maxresdefault.jpg"), QStringLiteral("bb.jpg"),
                                 QStringLiteral("source.jpg"), QStringLiteral("default.jpg")};
    if (generic.contains(name))
        return false;
    static const QRegularExpression sizeRe(QStringLiteral("^\\d+x\\d+(?:bb)?\\.(?:jpe?g|png|webp|avif|gif)$"),
                                            QRegularExpression::CaseInsensitiveOption);
    return !sizeRe.match(name).hasMatch();
}

QString coverLooseToken(const QString &url)
{
    const QString path = QUrl(url).path();
    QString best;
    QString current;
    const auto take = [&]() {
        if (current.size() >= 12 && current.size() > best.size() && !coverTokenIsGeneric(current))
            best = current;
        current.clear();
    };
    for (const QChar c : path) {
        if (c.isLetterOrNumber())
            current.append(c);
        else
            take();
    }
    take();
    return best;
}

bool coverUrlsMatch(const QString &wanted, const QString &loaded)
{
    if (wanted.isEmpty() || loaded.isEmpty())
        return false;
    if (wanted == loaded)
        return true;
    if (coverMatchKey(wanted) == coverMatchKey(loaded))
        return true;
    const QString file = QUrl(wanted).fileName();
    return coverFileIsDistinctive(file) && QUrl(loaded).fileName() == file;
}

QString coverDownloadReferer(const QString &homeUrl)
{
    const QUrl url(homeUrl.trimmed());
    if (!url.isValid() || url.host().isEmpty())
        return {};
    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        return {};
    return url.toString(QUrl::FullyEncoded);
}

QString expandCoverUrl(const QString &input)
{
    QString url = input.trimmed();
    if (url.isEmpty())
        return url;
    if (url.startsWith(QStringLiteral("//")))
        url.prepend(QStringLiteral("https:"));
    url.replace(QStringLiteral("%7B"), QStringLiteral("{"), Qt::CaseInsensitive);
    url.replace(QStringLiteral("%7D"), QStringLiteral("}"), Qt::CaseInsensitive);
    const QPair<const char *, const char *> subs[] = {
        {"{{width}}", "640"},  {"{width}", "640"},  {"{{height}}", "360"}, {"{height}", "360"},
        {"{{quality}}", "80"}, {"{quality}", "80"}, {"{{format}}", "jpg"}, {"{format}", "jpg"},
        {"{{size}}", "640x360"}, {"{size}", "640x360"}, {"{{w}}", "640"}, {"{w}", "640"},
        {"{{h}}", "360"},       {"{h}", "360"},       {"{{q}}", "80"},   {"{q}", "80"},
        {"{{f}}", "jpg"},       {"{f}", "jpg"},       {"{{c}}", "bb"},   {"{c}", "bb"},
    };
    for (const auto &sub : subs)
        url.replace(QLatin1String(sub.first), QLatin1String(sub.second), Qt::CaseInsensitive);
    if (url.contains(QStringLiteral("mzstatic.com"))
        && (url.endsWith(QStringLiteral("/source")) || url.endsWith(QStringLiteral("/source/")))) {
        while (url.endsWith(QLatin1Char('/')))
            url.chop(1);
        url += QStringLiteral("/640x360bb.jpg");
    }
    return url;
}

bool ftypMentionsAvif(const QByteArray &body)
{
    if (body.size() < 12 || body.mid(4, 4) != "ftyp")
        return false;
    const auto brandAt = [&](int offset) {
        return offset + 4 <= body.size() ? body.mid(offset, 4) : QByteArray();
    };
    const auto avifBrand = [](const QByteArray &brand) {
        return brand == "avif" || brand == "avis" || brand == "mif1" || brand == "miaf";
    };
    if (avifBrand(brandAt(8)))
        return true;
    int box = 0;
    for (int i = 0; i < 4; ++i)
        box = (box << 8) | static_cast<unsigned char>(body.at(i));
    if (box < 16 || box > body.size())
        box = qMin(body.size(), 64);
    const int limit = qMin(box, 64);
    for (int i = 16; i + 4 <= limit; i += 4) {
        if (avifBrand(brandAt(i)))
            return true;
    }
    return false;
}

QString coverKind(const QByteArray &body)
{
    if (body.size() >= 12 && body.startsWith("RIFF") && body.mid(8, 4) == "WEBP")
        return QStringLiteral("webp");
    if (ftypMentionsAvif(body))
        return QStringLiteral("avif");
    if (body.size() >= 3 && static_cast<unsigned char>(body.at(0)) == 0xFF
        && static_cast<unsigned char>(body.at(1)) == 0xD8)
        return QStringLiteral("jpeg");
    if (body.startsWith("\x89PNG"))
        return QStringLiteral("png");
    if (body.startsWith("GIF87a") || body.startsWith("GIF89a"))
        return QStringLiteral("gif");
    const QByteArray head = body.left(24).trimmed().toLower();
    if (head.startsWith("<!") || head.startsWith("<ht") || head.startsWith("<htm") || head.startsWith("{")
        || head.startsWith("["))
        return QStringLiteral("texto");
    return QStringLiteral("bin");
}

QString coverBodyLabel(const QByteArray &body)
{
    return coverKind(body) + QLatin1Char(' ') + QString::fromLatin1(body.left(8).toHex());
}

QByteArray encodeJpeg(const QImage &image)
{
    if (image.isNull() || image.width() < 24 || image.height() < 24)
        return {};
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "JPEG", 82) || jpeg.size() < 32)
        return {};
    return jpeg;
}

QImage imageFromCover(const QByteArray &body, const QByteArray &format)
{
    QBuffer buffer;
    buffer.setData(body);
    if (!buffer.open(QIODevice::ReadOnly))
        return {};
    QImageReader reader(&buffer, format);
    reader.setAutoTransform(true);
    return reader.read();
}

QByteArray ffmpegJpeg(const QByteArray &body)
{
    // AVIF é ISOBMFF: o ffmpeg só acha o átomo se puder buscar. O pipe não busca,
    // e -f avif neste ffmpeg é só muxer. O arquivo temporário deixa a sonda ver a marca.
    QTemporaryFile tmp;
    if (!tmp.open() || tmp.write(body) != body.size())
        return {};
    tmp.flush();
    const QString path = tmp.fileName();
    tmp.close();
    QProcess proc;
    proc.start(QStringLiteral("ffmpeg"),
               {QStringLiteral("-nostdin"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                QStringLiteral("error"), QStringLiteral("-i"), path, QStringLiteral("-an"),
                QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-f"), QStringLiteral("image2pipe"),
                QStringLiteral("-vcodec"), QStringLiteral("mjpeg"), QStringLiteral("-q:v"), QStringLiteral("3"),
                QStringLiteral("pipe:1")});
    if (!proc.waitForStarted(2000))
        return {};
    if (!proc.waitForFinished(8000)) {
        proc.kill();
        proc.waitForFinished(400);
        return {};
    }
    const QByteArray out = proc.readAllStandardOutput();
    if (out.size() < 32 || static_cast<unsigned char>(out.at(0)) != 0xFF || static_cast<unsigned char>(out.at(1)) != 0xD8)
        return {};
    return out;
}

void logImageFormatsOnce()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    QStringList names;
    for (const QByteArray &fmt : QImageReader::supportedImageFormats())
        names.append(QString::fromLatin1(fmt));
    std::fprintf(stderr, "zappe: formatos de imagem Qt: %s\n", qPrintable(names.join(QLatin1Char(' '))));
}

QByteArray coverJpegFromBody(const QByteArray &body, QString *detail, const QString &mime)
{
    const auto note = [detail](const QString &text) {
        if (detail)
            *detail = text;
    };
    if (body.size() < 32) {
        note(QStringLiteral("curto %1").arg(coverBodyLabel(body)));
        return {};
    }
    const QString kind = coverKind(body);
    QByteArray format;
    if (kind == QLatin1String("jpeg"))
        format = "jpeg";
    else if (kind == QLatin1String("png") || kind == QLatin1String("gif") || kind == QLatin1String("webp")
             || kind == QLatin1String("avif"))
        format = kind.toLatin1();
    QImage image;
    if (!format.isEmpty())
        image = imageFromCover(body, format);
    if (image.isNull())
        image.loadFromData(body);
    if (!image.isNull() && (image.width() < 24 || image.height() < 24)) {
        note(QStringLiteral("pequena %1x%2 %3").arg(image.width()).arg(image.height()).arg(coverBodyLabel(body)));
        return {};
    }
    QByteArray jpeg = encodeJpeg(image);
    if (jpeg.size() >= 32) {
        note(QString());
        return jpeg;
    }
    Q_UNUSED(mime);
    jpeg = ffmpegJpeg(body);
    if (jpeg.size() >= 32) {
        const QImage check = imageFromCover(jpeg, "jpeg");
        if (check.isNull() || check.width() < 24 || check.height() < 24) {
            note(QStringLiteral("pequena ffmpeg %1").arg(coverBodyLabel(body)));
            return {};
        }
        note(QString());
        return jpeg;
    }
    note(coverBodyLabel(body));
    return {};
}

bool coverFetchIsBlocked(const QString &error)
{
    const QString text = error.toLower();
    return text.contains(QStringLiteral("failed to fetch")) || text.contains(QStringLiteral("networkerror"))
           || text.contains(QStringLiteral("load failed")) || text.contains(QStringLiteral("access-control"))
           || text.contains(QStringLiteral("cors")) || text.contains(QStringLiteral("opaque"));
}

QString mirrorCoverClipScript(const QStringList &urls, const QStringList &titles)
{
    QStringList quoted;
    QStringList quotedTitles;
    for (int i = 0; i < urls.size(); ++i) {
        quoted.append(jsString(urls.at(i)));
        quotedTitles.append(jsString(i < titles.size() ? titles.at(i) : QString()));
    }
    return QStringLiteral("(() => { const urls = [") + quoted.join(QLatin1Char(','))
           + QStringLiteral("]; const titles = [") + quotedTitles.join(QLatin1Char(','))
           + QStringLiteral(R"js(];
  const tail = (url) => {
    try {
      const path = new URL(url, location.href).pathname || '';
      const parts = path.split('/');
      return parts[parts.length - 1] || '';
    } catch (e) { return ''; }
  };
  const blobOf = (el) => {
    if (!el) return '';
    const bits = [];
    if (el.currentSrc) bits.push(el.currentSrc);
    if (el.src) bits.push(el.src);
    if (el.getAttribute) {
      bits.push(el.getAttribute('src') || '');
      bits.push(el.getAttribute('srcset') || '');
      bits.push(el.getAttribute('data-src') || '');
      bits.push(el.getAttribute('data-srcset') || '');
    }
    bits.push((el.style && el.style.backgroundImage) || '');
    return bits.join(' ');
  };
  const nodes = [...document.querySelectorAll('img, source, [style*="background"]')];
  const vw = window.innerWidth || 1920;
  const vh = window.innerHeight || 1080;
  const rects = [];
  for (let i = 0; i < urls.length; i++) {
    const url = urls[i];
    if (rects.length >= 6) break;
    const key = tail(url);
    const bare = String(url).split('?')[0];
    let el = null;
    for (const node of nodes) {
      const blob = blobOf(node);
      if (!blob) continue;
      const generic = key === '640x360bb.jpg' || key === 'hqdefault.jpg' || key === 'mqdefault.jpg'
        || key === 'sddefault.jpg' || key === 'maxresdefault.jpg';
      if (blob.indexOf(url) >= 0 || (bare.length > 12 && blob.indexOf(bare) >= 0)
          || (key.length >= 12 && !generic && blob.indexOf(key) >= 0)) {
        el = node;
        break;
      }
    }
    const title = String(titles[i] || '').replace(/\s+/g, ' ').trim().toLowerCase();
    if (!el && title.length >= 4) {
      const cards = document.querySelectorAll('a, [aria-label], [role="link"]');
      for (const card of cards) {
        const name = ((card.getAttribute && (card.getAttribute('aria-label') || '')) || card.innerText || '')
          .replace(/\s+/g, ' ').trim().toLowerCase();
        if (!name || name.length > 120) continue;
        const prefix = name === title || name.indexOf(title) === 0;
        const contains = title.length >= 6 && name.indexOf(title) >= 0;
        if (!prefix && !contains) continue;
        el = (card.querySelector && card.querySelector('img, source')) || card;
        break;
      }
    }
    if (!el) continue;
    let r = el.getBoundingClientRect();
    if (r.width < 40 || r.height < 40) {
      const parent = el.parentElement;
      if (parent) r = parent.getBoundingClientRect();
    }
    if (r.width < 40 || r.height < 40) continue;
    if (r.bottom < 8 || r.top > vh - 8 || r.right < 8 || r.left > vw - 8) continue;
    const x = Math.max(0, Math.round(r.left));
    const y = Math.max(0, Math.round(r.top));
    const w = Math.round(Math.min(r.width, vw - x));
    const h = Math.round(Math.min(r.height, vh - y));
    if (w < 40 || h < 40) continue;
    rects.push({url: url, x: x, y: y, w: w, h: h});
  }
  return JSON.stringify({rects: rects});
})())js");
}

bool coverUrlIsPlaceholder(const QString &url)
{
    const QString lower = url.toLower();
    const bool gif = lower.contains(QStringLiteral(".gif"));
    if (!gif)
        return false;
    return lower.contains(QStringLiteral("1x1")) || lower.contains(QStringLiteral("pixel"))
           || lower.contains(QStringLiteral("spacer"));
}

bool mirrorKeepsProfiles(const MirrorAdapter &adapter)
{
    return adapter.kind.compare(QStringLiteral("youtube"), Qt::CaseInsensitive) != 0
           && adapter.id.compare(QStringLiteral("youtube"), Qt::CaseInsensitive) != 0;
}

bool mirrorProfileLabel(const QString &name)
{
    const QString lower = name.trimmed().toLower();
    if (lower.size() < 2)
        return false;
    if (lower == QLatin1String("account menu") || lower == QLatin1String("menu da conta")
        || lower == QLatin1String("perfis") || lower == QLatin1String("profiles")
        || lower.contains(QStringLiteral("account menu")) || lower.contains(QStringLiteral("menu da conta")))
        return false;
    return true;
}

bool mirrorLiveHero(const QString &title, const QString &href)
{
    Q_UNUSED(href);
    const QString name = title.trimmed().toLower();
    if (name.startsWith(QStringLiteral("assistir ao vivo")))
        return true;
    return name == QLatin1String("ao vivo") || name.startsWith(QStringLiteral("ao vivo:"));
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
    if (mirrorKeepsProfiles(adapter) && !page.profiles.isEmpty()) {
        ShelfData profiles;
        profiles.id = skill + QStringLiteral(":profiles");
        profiles.skillId = skill;
        profiles.title = QStringLiteral("Perfis");
        profiles.status = QStringLiteral("ok");
        for (const QString &name : page.profiles) {
            if (!mirrorProfileLabel(name))
                continue;
            RowData row;
            row.title = name;
            row.service = adapter.id;
            row.serviceLabel = adapter.name;
            row.href = QStringLiteral("zappe://profile/") + QString::fromUtf8(QUrl::toPercentEncoding(name));
            profiles.rows.append(row);
        }
        if (!profiles.rows.isEmpty())
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
            if (name.isEmpty() || href.isEmpty() || href.startsWith(QStringLiteral("zappe://")) || titleRejected(adapter, name)
                || mirrorLiveHero(name, href))
                continue;
            RowData row;
            row.title = name;
            row.service = adapter.id;
            row.serviceLabel = adapter.name;
            row.href = href;
            const QString art = expandCoverUrl(rowObj.value(QStringLiteral("artwork")).toString());
            row.artworkSpec = coverUrlIsPlaceholder(art) ? QString() : art;
            row.synopsis = rowObj.value(QStringLiteral("synopsis")).toString().simplified();
            if (row.synopsis.compare(name, Qt::CaseInsensitive) == 0)
                row.synopsis.clear();
            if (row.synopsis.size() > 280)
                row.synopsis = row.synopsis.left(277) + QStringLiteral("…");
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
    return mirrorTitleCount(page) > 0;
}

int mirrorTitleCount(const MirrorPage &page)
{
    int count = 0;
    for (const ShelfData &shelf : page.shelves) {
        if (shelf.id.endsWith(QStringLiteral(":profiles")) || shelf.status != QLatin1String("ok"))
            continue;
        count += shelf.rows.size();
    }
    return count;
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
    const QByteArray hash =
        QCryptographicHash::hash(coverMatchKey(expandCoverUrl(url)).toUtf8(), QCryptographicHash::Sha1).toHex();
    QDir dir(cacheDir);
    dir.mkpath(QStringLiteral("covers"));
    return dir.filePath(QStringLiteral("covers/%1.jpg").arg(QString::fromLatin1(hash)));
}

bool coverIsFresh(const QString &path, const QDateTime &now, int maxAgeSecs)
{
    const QFileInfo info(path);
    if (!info.exists() || info.size() < 32)
        return false;
    const qint64 age = info.lastModified().secsTo(now);
    return age >= -60 && age <= maxAgeSecs;
}

int assignCoverFile(QVector<ShelfData> &shelves, const QString &url, const QString &path, bool *changed)
{
    int linked = 0;
    bool didChange = false;
    for (ShelfData &shelf : shelves) {
        for (RowData &row : shelf.rows) {
            if (!coverUrlsMatch(row.artworkSpec, url))
                continue;
            ++linked;
            if (row.artwork != path) {
                row.artwork = path;
                didChange = true;
            }
        }
    }
    if (changed)
        *changed = didChange;
    if (linked > 0)
        touchCoverFile(path);
    return linked;
}

int purgeStaleCovers(const QString &cacheDir, const QDateTime &now)
{
    QDir dir(QDir(cacheDir).filePath(QStringLiteral("covers")));
    if (!dir.exists())
        return 0;
    int removed = 0;
    const QFileInfoList files = dir.entryInfoList(QDir::Files);
    for (const QFileInfo &info : files) {
        const QString name = info.fileName().toLower();
        const bool legacy = name.endsWith(QStringLiteral(".img"));
        const qint64 age = info.lastModified().secsTo(now);
        if (!legacy && !(name.endsWith(QStringLiteral(".jpg")) && age > mirrorCoverUnusedMaxAgeSecs()))
            continue;
        if (QFile::remove(info.absoluteFilePath()))
            ++removed;
    }
    return removed;
}

QVector<CoverJob> coverJobs(const QVector<ShelfData> &shelves, const QString &cacheDir, const QDateTime &now)
{
    QVector<CoverJob> jobs;
    QSet<QString> seen;
    for (const ShelfData &shelf : shelves) {
        for (const RowData &row : shelf.rows) {
            if (!row.artworkSpec.startsWith(QStringLiteral("http")))
                continue;
            const QString path = coverCachePath(cacheDir, row.artworkSpec);
            if (seen.contains(path) || coverIsFresh(path, now, mirrorCoverMaxAgeSecs()))
                continue;
            seen.insert(path);
            CoverJob job;
            job.serviceId = shelf.skillId.mid(QStringLiteral("mirror:").size());
            job.url = row.artworkSpec;
            job.path = path;
            job.title = row.title;
            jobs.append(job);
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
    purgeStaleCovers(m_cacheDir, QDateTime::currentDateTime());
    m_cdp = new CdpClient(this);
    connect(m_cdp, &CdpClient::attached, this, &MirrorSession::onAttached);
    connect(m_cdp, &CdpClient::navigated, this, &MirrorSession::onNavigated);
    connect(m_cdp, &CdpClient::evaluated, this, &MirrorSession::onEvaluated);
    connect(m_cdp, &CdpClient::networkResponse, this, &MirrorSession::onNetworkResponse);
    connect(m_cdp, &CdpClient::networkFinished, this, &MirrorSession::onNetworkFinished);
    connect(m_cdp, &CdpClient::responseBody, this, &MirrorSession::onResponseBody);
    connect(m_cdp, &CdpClient::screenshotReady, this, &MirrorSession::onScreenshot);
    connect(m_cdp, &CdpClient::frameNavigated, this, &MirrorSession::onFrame);
    connect(m_cdp, &CdpClient::failed, this, [this](const QString &message) {
        std::fprintf(stderr, "zappe: espelho cdp falhou: %s\n", qPrintable(message));
        if (m_job == JobLearnDom || m_job == JobLearnShot)
            return;
        if (m_job == JobCovers) {
            if (!m_coverFetchGaveUp) {
                for (const CoverJob &job : m_coverBatch)
                    m_covers.append(job);
                for (const CoverJob &job : m_clipJobs)
                    m_covers.append(job);
            }
            m_coverBatch.clear();
            m_clipJobs.clear();
            if (m_coverError.isEmpty())
                m_coverError = message.left(140);
            finishCoverPass();
            return;
        }
        // Navegação substituída no mesmo alvo. A extração da navegação nova segue.
        // No player, ERR_ABORTED no watch deixa a kiosk em about:blank.
        if (message.contains(QStringLiteral("ERR_ABORTED"))) {
            if (m_job != JobPlay)
                return;
            if (m_playNavTries < 1 && m_chrome && m_chrome->face() == ChromeFace::Player && m_cdp && m_cdp->connected()
                && !m_pendingUrl.isEmpty()) {
                ++m_playNavTries;
                std::fprintf(stderr, "zappe: espelho repetindo o navigate do player\n");
                m_cdp->navigate(m_pendingUrl);
                return;
            }
            emit failed(QStringLiteral("player em about:blank"));
            return;
        }
        const bool down = !m_cdp || !m_cdp->connected();
        if (down) {
            m_holdNav = false;
            // O Chrome do Xvfb cai no present(). O navigate do watch espera o kiosk novo.
            if (m_job == JobPlay)
                return;
            if ((m_job == JobExtract || m_job == JobSearch) && !m_current.isEmpty())
                m_queue.prepend(m_current);
            m_busy = false;
            m_job = JobNone;
            m_tabService.clear();
            schedulePump();
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
        m_holdNav = false;
        // A aba nova não chegou. Não reusar o alvo de outro serviço.
        m_tabService.clear();
        QTimer::singleShot(400, this, &MirrorSession::pump);
    });
    connect(m_chrome, &ChromeSession::ready, this, &MirrorSession::onChromeReady);
    connect(m_chrome, &ChromeSession::failed, this, [this](const QString &message) { emit failed(message); });
    m_refresh = new QTimer(this);
    m_refresh->setInterval(mirrorRefreshMs());
    connect(m_refresh, &QTimer::timeout, this, [this] {
        purgeStaleCovers(m_cacheDir, QDateTime::currentDateTime());
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
    m_tabService.clear();
    if (m_job == JobPlay) {
        scheduleAttach();
        return;
    }
    if (m_busy)
        return;
    if (m_queue.isEmpty()) {
        for (const MirrorAdapter &adapter : m_adapters)
            m_queue.append(adapter.id);
    }
    pump();
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

void MirrorSession::schedulePump()
{
    if (!m_cdp || !m_chrome || m_busy)
        return;
    if (m_attachTries >= 12) {
        std::fprintf(stderr, "zappe: espelho sem CDP na porta %d\n", m_chrome->port());
        return;
    }
    const int attempt = m_attachTries++;
    int delay = attempt == 0 ? 200 : 250;
    for (int i = 1; i < attempt && delay < 2000; ++i)
        delay *= 2;
    delay = std::min(delay, 2000);
    QTimer::singleShot(delay, this, [this] {
        if (!m_busy)
            pump();
    });
}

void MirrorSession::onAttached()
{
    m_attachTries = 0;
    if (m_cdp)
        m_cdp->enableNetwork();
    if (m_job == JobPlay) {
        if (m_chrome && m_chrome->face() == ChromeFace::Player && m_cdp && !m_pendingUrl.isEmpty())
            m_cdp->navigate(m_pendingUrl);
        return;
    }
    if (m_holdNav) {
        const QString url = m_openUrl;
        m_holdNav = false;
        m_openUrl.clear();
        if (mirrorUrlIsPlayback(url)) {
            std::fprintf(stderr, "zappe: espelho recusou URL de playback no catálogo: %s\n", qPrintable(url));
            return;
        }
        if (!url.isEmpty() && m_cdp)
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
    m_holdNav = false;
    m_openUrl.clear();
    m_forceReload = false;
    m_sawPlayback = false;
    m_playNavTries = 0;
    m_queue.clear();
    m_samples.clear();
    m_inflight.clear();
    m_images.clear();
    m_imageInflight.clear();
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
    if ((job == JobExtract || job == JobSearch) && mirrorUrlIsPlayback(url)) {
        std::fprintf(stderr, "zappe: espelho recusou URL de playback no catálogo: %s\n", qPrintable(url));
        m_busy = false;
        m_job = JobNone;
        m_forceReload = false;
        QTimer::singleShot(400, this, &MirrorSession::pump);
        return;
    }
    if (!m_cdp || !m_chrome) {
        m_queue.removeAll(serviceId);
        m_queue.prepend(serviceId);
        m_busy = false;
        m_job = JobNone;
        return;
    }
    ++m_epoch;
    m_feedPending = false;
    m_current = serviceId;
    const QString previous = m_navUrl;
    m_navUrl = url;
    if (job == JobSearch)
        m_extraLeft.clear();
    m_job = job;
    m_busy = true;
    m_busySince = QDateTime::currentMSecsSinceEpoch();
    m_learnPosted = false;
    m_samples.clear();
    m_inflight.clear();
    m_images.clear();
    m_imageInflight.clear();
    m_coverAttempted.clear();
    m_artLogged.remove(serviceId);
    m_coverLogged.remove(serviceId);
    m_lastFeedTitles = -1;
    m_lastDomTitles = -1;
    std::fprintf(stderr, "zappe: espelho %s -> %s\n", qPrintable(serviceId), qPrintable(url));
    // Histórico do mesmo serviço reusa a aba. Outro serviço ganha um target novo,
    // mesmo sem websocket ainda: a primeira home não pode esperar um attach compartilhado.
    const bool sameTab = m_cdp->connected() && m_tabService == serviceId && !m_holdNav;
    if (sameTab) {
        // Page.navigate da mesma URL é no-op no YouTube e relê um DOM virtualizado.
        m_forceReload = !previous.isEmpty() && previous == url;
        m_cdp->navigate(m_forceReload ? QStringLiteral("about:blank") : url);
        return;
    }
    m_forceReload = false;
    m_tabService = serviceId;
    m_openUrl = url;
    m_holdNav = true;
    m_cdp->openPage(m_chrome->port(), QStringLiteral("about:blank"));
}

void MirrorSession::pump()
{
    if (m_player || m_busy || m_queue.isEmpty() || !m_cdp || !m_chrome)
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
    if (m_forceReload) {
        m_forceReload = false;
        if (!m_player && m_cdp && m_cdp->connected() && !m_navUrl.isEmpty())
            m_cdp->navigate(m_navUrl);
        return;
    }
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
    const MirrorAdapter *adapter = findMirrorAdapter(m_adapters, serviceId);
    const bool haveTitles = adapter && mirrorPageHasTitles(parseMirrorFeeds(m_samples, *adapter));
    // Não troca de serviço enquanto o feed ainda pode chegar. O próximo Page.navigate
    // fica numa aba nova, depois desta contagem.
    if ((!haveTitles || !m_inflight.isEmpty()) && waited < 8000) {
        QTimer::singleShot(400, this, [this, epoch, serviceId, job, waited] { settle(epoch, serviceId, job, waited + 400); });
        return;
    }
    considerExtract();
}

int httpArtworkCount(const MirrorPage &page)
{
    int count = 0;
    for (const ShelfData &shelf : page.shelves) {
        if (shelf.id.endsWith(QStringLiteral(":profiles")))
            continue;
        for (const RowData &row : shelf.rows) {
            if (row.artworkSpec.startsWith(QStringLiteral("http")))
                ++count;
        }
    }
    return count;
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
    m_lastFeedTitles = mirrorTitleCount(network);
    std::fprintf(stderr, "zappe: espelho %s feed %d títulos em %d corpos\n", qPrintable(m_current),
                 m_lastFeedTitles, int(m_samples.size()));
    for (const NetworkSample &sample : m_samples) {
        const int n = mirrorTitleCount(parseMirrorFeeds(QVector<NetworkSample>{sample}, *adapter));
        std::fprintf(stderr, "zappe: espelho %s feed %d títulos em %s\n", qPrintable(m_current), n,
                     qPrintable(sample.url.left(180)));
    }
    if (mirrorPageHasTitles(network) && httpArtworkCount(network) > 0) {
        m_feedPending = false;
        acceptPage(network);
        return;
    }
    if (mirrorPageHasTitles(network)) {
        m_feedPage = network;
        m_feedPending = true;
        std::fprintf(stderr, "zappe: espelho %s feed sem arte; extrator DOM\n", qPrintable(m_current));
    } else {
        m_feedPending = false;
    }
    std::fprintf(stderr, "zappe: espelho %s extrator DOM de mirrors.json\n", qPrintable(m_current));
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
    m_resumeExtra = extract && !tagged.needsLogin && !m_extraLeft.isEmpty() && !m_player;
    if (!applyPage(tagged)) {
        finishAccept();
        return;
    }
    noteArtwork(tagged);
    m_coverClosed = false;
    m_coverFromNet = 0;
    m_coverFromFetch = 0;
    m_coverFromClip = 0;
    m_coverFetched = 0;
    m_coverFetchGaveUp = true;
    m_coverError.clear();
    m_coverSample.clear();
    m_coverPhase = 0;
    m_clipJobs.clear();
    m_shots.clear();
    m_shotIndex = 0;
    m_coverBatch.clear();
    m_coverFromNet = takeHeldCovers();
    QVector<CoverJob> http;
    QVector<CoverJob> rest;
    for (const CoverJob &job : m_covers) {
        if (job.serviceId == m_current)
            http.append(job);
        else
            rest.append(job);
    }
    m_covers = rest;
    if (http.size() > 6)
        m_clipJobs = http.mid(0, 6);
    else
        m_clipJobs = http;
    startDirectCovers(http);
    if (!startCoverClips())
        finishCoverPass();
}

void MirrorSession::finishEmpty()
{
    m_busy = false;
    m_job = JobNone;
    if (mirrorHoldLoading(shelfTitleCount(m_pages.value(m_current)), 0)) {
        std::fprintf(stderr, "zappe: espelho %s leitura vazia; segue o loading\n", qPrintable(m_current));
        QTimer::singleShot(500, this, &MirrorSession::pump);
        return;
    }
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
        std::fprintf(stderr,
                     "zappe: espelho %s extrator estático sem catálogo (feed %d, dom %d); aprendizado desligado\n",
                     qPrintable(serviceId), m_lastFeedTitles, m_lastDomTitles);
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
    if (requestId.isEmpty() || m_player || url.isEmpty())
        return;
    if (imageResponse(url, mimeType) && !url.startsWith(QStringLiteral("data:")) && !url.startsWith(QStringLiteral("blob:"))) {
        const QString lower = url.toLower();
        if (lower.contains(QStringLiteral("1x1")) || lower.contains(QStringLiteral("pixel"))
            || lower.contains(QStringLiteral("analytics")))
            return;
        if (m_images.size() + m_imageInflight.size() >= 40)
            return;
        if (!m_imageInflight.contains(requestId))
            m_imageInflight.insert(requestId, url.left(500));
        return;
    }
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
    if (!m_cdp)
        return;
    if (m_inflight.contains(requestId) || m_imageInflight.contains(requestId))
        m_cdp->getResponseBody(requestId);
}

void MirrorSession::onResponseBody(const QString &requestId, const QByteArray &body, bool ok)
{
    if (m_imageInflight.contains(requestId)) {
        const QString imageUrl = m_imageInflight.take(requestId);
        if (ok && !imageUrl.isEmpty())
            rememberImage(imageUrl, body);
        return;
    }
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
    if (m_job == JobLearnShot) {
        postLearn(ok ? png : QByteArray());
        return;
    }
    if (m_job != JobCovers || m_coverPhase != 3)
        return;
    if (m_shotIndex >= 0 && m_shotIndex < m_shots.size()) {
        const CoverJob job = m_shots.at(m_shotIndex).job;
        QImage image;
        if (ok && png.size() >= 32)
            image.loadFromData(png);
        if (!image.isNull() && image.width() >= 20 && image.height() >= 20) {
            storeCover(job, png);
            ++m_coverFromClip;
        } else if (!m_coverFetchGaveUp) {
            m_covers.append(job);
        }
        ++m_shotIndex;
    }
    shootNextClip();
}

void MirrorSession::onFrame(const QString &url)
{
    if (!m_player || m_job != JobPlay)
        return;
    const bool blank = url.isEmpty() || url == QLatin1String("about:blank") || url.startsWith(QStringLiteral("about:blank"));
    if (blank) {
        if (!m_sawPlayback)
            return;
        std::fprintf(stderr, "zappe: player caiu em about:blank; fechando o kiosk\n");
        emit failed(QStringLiteral("player em about:blank"));
        return;
    }
    if (url.startsWith(QStringLiteral("http://")) || url.startsWith(QStringLiteral("https://")))
        m_sawPlayback = true;
}

void MirrorSession::onEvaluated(const QString &value, bool ok)
{
    if (m_job == JobCovers) {
        const int gen = m_coverGen;
        if (m_coverPhase == 2) {
            const QJsonObject root = QJsonDocument::fromJson(value.toUtf8()).object();
            const QJsonArray rects = root.value(QStringLiteral("rects")).toArray();
            QSet<QString> hit;
            m_shots.clear();
            for (const QJsonValue &item : rects) {
                if (m_shots.size() >= 6)
                    break;
                const QJsonObject obj = item.toObject();
                const QString url = obj.value(QStringLiteral("url")).toString();
                const int w = obj.value(QStringLiteral("w")).toInt();
                const int h = obj.value(QStringLiteral("h")).toInt();
                if (url.isEmpty() || w < 40 || h < 40)
                    continue;
                for (const CoverJob &job : m_clipJobs) {
                    if (job.url != url)
                        continue;
                    CoverShot shot;
                    shot.job = job;
                    shot.x = obj.value(QStringLiteral("x")).toInt();
                    shot.y = obj.value(QStringLiteral("y")).toInt();
                    shot.w = w;
                    shot.h = h;
                    m_shots.append(shot);
                    hit.insert(url);
                    break;
                }
            }
            for (const CoverJob &job : m_clipJobs) {
                if (!hit.contains(job.url) && !m_coverFetchGaveUp)
                    m_covers.append(job);
            }
            m_clipJobs.clear();
            if (!ok && m_coverError.isEmpty())
                m_coverError = QStringLiteral("recorte sem resposta");
            if (m_coverGen != gen)
                return;
            if (m_shots.isEmpty()) {
                finishCoverPass();
                return;
            }
            m_coverPhase = 3;
            m_shotIndex = 0;
            shootNextClip();
            return;
        }
        QHash<QString, QByteArray> got;
        QHash<QString, QString> errors;
        bool parsed = false;
        if (ok) {
            const QJsonDocument doc = QJsonDocument::fromJson(value.toUtf8());
            if (doc.isArray()) {
                parsed = true;
                for (const QJsonValue &item : doc.array()) {
                    const QJsonObject obj = item.toObject();
                    const QString url = obj.value(QStringLiteral("url")).toString();
                    if (!obj.value(QStringLiteral("ok")).toBool()) {
                        const QString err = obj.value(QStringLiteral("err")).toString();
                        if (!url.isEmpty() && !err.isEmpty())
                            errors.insert(url, err);
                        continue;
                    }
                    const QByteArray body = QByteArray::fromBase64(obj.value(QStringLiteral("b64")).toString().toLatin1());
                    if (body.size() >= 32)
                        got.insert(url, body);
                }
            } else if (m_coverError.isEmpty()) {
                m_coverError = value.trimmed().left(160);
                if (m_coverError.isEmpty())
                    m_coverError = QStringLiteral("avaliacao vazia");
            }
        } else if (m_coverError.isEmpty()) {
            m_coverError = QStringLiteral("avaliacao sem resultado");
        }
        if (m_job != JobCovers || m_coverGen != gen)
            return;
        ++m_coverGen;
        int saved = 0;
        bool blocked = !ok || !parsed;
        for (const CoverJob &job : m_coverBatch) {
            const QByteArray body = got.value(job.url);
            QImage image;
            if (!body.isEmpty())
                image.loadFromData(body);
            if (!image.isNull()) {
                storeCover(job, body);
                ++saved;
            } else {
                m_clipJobs.append(job);
                const QString err = errors.value(job.url);
                if (m_coverError.isEmpty() && !err.isEmpty())
                    m_coverError = err;
                if (coverFetchIsBlocked(err))
                    blocked = true;
            }
        }
        m_coverFromFetch += saved;
        m_coverBatch.clear();
        if (saved == 0 && blocked)
            m_coverFetchGaveUp = true;
        if (!fetchNextCoverBatch())
            finishCoverPass();
        return;
    }
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
    const QString extractError =
        QJsonDocument::fromJson(value.toUtf8()).object().value(QStringLiteral("error")).toString();
    if (!extractError.isEmpty())
        std::fprintf(stderr, "zappe: espelho %s extrator falhou: %s\n", qPrintable(m_current),
                     qPrintable(extractError.left(180)));
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
        if (m_feedPending && mirrorPageHasTitles(m_feedPage)) {
            m_feedPending = false;
            acceptPage(m_feedPage);
            return;
        }
        beginRepair();
        return;
    }
    MirrorPage page = parseMirrorPage(value.toUtf8(), *adapter);
    if (m_feedPending) {
        const int domArt = httpArtworkCount(page);
        const int feedArt = httpArtworkCount(m_feedPage);
        if ((domArt == 0 && feedArt > 0) || (!mirrorPageHasTitles(page) && mirrorPageHasTitles(m_feedPage))
            || (domArt == 0 && feedArt == 0 && mirrorTitleCount(m_feedPage) > mirrorTitleCount(page)))
            page = m_feedPage;
        m_feedPending = false;
    }
    m_lastDomTitles = mirrorTitleCount(page);
    std::fprintf(stderr, "zappe: espelho %s dom %d títulos login=%d\n", qPrintable(m_current), m_lastDomTitles,
                 page.needsLogin ? 1 : 0);
    if (page.needsLogin || mirrorPageHasTitles(page)) {
        acceptPage(page);
        return;
    }
    beginRepair();
}

bool MirrorSession::applyPage(const MirrorPage &page)
{
    if (!m_guide || m_current.isEmpty())
        return false;
    const MirrorAdapter *adapter = findMirrorAdapter(m_adapters, m_current);
    if (!adapter)
        return false;
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
    if (!m_mergePage && !page.needsLogin) {
        const int previous = shelfTitleCount(m_pages.value(adapter->id));
        const int incoming = shelfTitleCount(shelves);
        if (mirrorKeepRicherPage(previous, incoming)) {
            std::fprintf(stderr, "zappe: espelho %s manteve %d títulos (a leitura nova tem %d)\n", qPrintable(adapter->id),
                         previous, incoming);
            return false;
        }
    }
    m_loadToken.insert(adapter->id, m_loadToken.value(adapter->id) + 1);
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
        return false;
    paintMissingCovers(shelves, m_cacheDir, QDateTime::currentDateTime());
    m_pages.insert(adapter->id, shelves);
    m_guide->replaceServiceShelves(adapter->id, shelves);
    const QVector<CoverJob> jobs = coverJobs(shelves, m_cacheDir, QDateTime::currentDateTime());
    for (const CoverJob &job : jobs) {
        bool queued = false;
        for (const CoverJob &have : m_covers) {
            if (have.path == job.path || coverUrlsMatch(have.url, job.url))
                queued = true;
        }
        if (!queued) {
            CoverJob queuedJob{adapter->id, job.url, job.path, adapter->homeUrl, job.title};
            m_covers.append(queuedJob);
        }
    }
    return true;
}

void MirrorSession::noteArtwork(const MirrorPage &page)
{
    if (m_artLogged.contains(m_current))
        return;
    m_artLogged.insert(m_current);
    int http = 0;
    int empty = 0;
    QStringList samples;
    for (const ShelfData &shelf : page.shelves) {
        if (shelf.id.endsWith(QStringLiteral(":profiles")))
            continue;
        for (const RowData &row : shelf.rows) {
            if (row.artworkSpec.startsWith(QStringLiteral("http"))) {
                ++http;
                if (samples.size() < 3)
                    samples.append(row.artworkSpec.left(120));
            } else if (!row.title.isEmpty()) {
                ++empty;
            }
        }
    }
    if (m_coverSample.isEmpty())
        m_coverSample = samples.join(QLatin1Char(' '));
    std::fprintf(stderr, "zappe: espelho %s artes %d http, %d sem url%s\n", qPrintable(m_current), http, empty,
                 samples.isEmpty() ? "" : qPrintable(QStringLiteral("; amostra ") + samples.join(QLatin1Char(' '))));
}

QByteArray MirrorSession::heldImage(const QString &url) const
{
    const QString key = coverMatchKey(url);
    const QString file = QUrl(url).fileName();
    const bool fileOk = coverFileIsDistinctive(file);
    const QString token = coverLooseToken(url);
    QByteArray byFile;
    int fileHits = 0;
    QByteArray byToken;
    int tokenHits = 0;
    for (const HeldImage &image : m_images) {
        if (image.url == url || image.key == key)
            return image.body;
        if (fileOk && image.file == file) {
            byFile = image.body;
            ++fileHits;
        }
        if (!token.isEmpty() && coverLooseToken(image.url) == token) {
            byToken = image.body;
            ++tokenHits;
        }
    }
    if (fileHits == 1)
        return byFile;
    if (tokenHits == 1)
        return byToken;
    return {};
}

void MirrorSession::rememberImage(const QString &url, const QByteArray &body)
{
    if (url.isEmpty() || body.size() < 32 || body.size() > 500000 || m_images.size() >= 40)
        return;
    const QString key = coverMatchKey(url);
    for (const HeldImage &image : m_images) {
        if (image.key == key)
            return;
    }
    const QString kind = coverKind(body);
    if (kind == QLatin1String("texto") || kind == QLatin1String("bin"))
        return;
    if (kind == QLatin1String("jpeg") || kind == QLatin1String("png") || kind == QLatin1String("gif")) {
        QImage probe;
        if (!probe.loadFromData(body) || probe.width() < 80 || probe.height() < 80)
            return;
    }
    HeldImage held;
    held.url = url.left(500);
    held.key = key;
    held.file = QUrl(url).fileName();
    held.body = body;
    m_images.append(held);
    attachHeldCover(held.url, body);
}

int MirrorSession::takeHeldCovers()
{
    int saved = 0;
    QVector<CoverJob> left;
    for (const CoverJob &job : m_covers) {
        if (job.serviceId != m_current) {
            left.append(job);
            continue;
        }
        const QByteArray body = heldImage(job.url);
        QString detail;
        const QByteArray jpeg = body.isEmpty() ? QByteArray() : coverJpegFromBody(body, &detail);
        if (jpeg.size() >= 32) {
            storeCover(job, jpeg);
            ++saved;
        } else {
            left.append(job);
        }
    }
    m_covers = left;
    return saved;
}

void MirrorSession::dropCurrentCoverJobs()
{
    QVector<CoverJob> keep;
    for (const CoverJob &job : m_covers) {
        if (job.serviceId != m_current)
            keep.append(job);
    }
    m_covers = keep;
}

void MirrorSession::logCoverSummary()
{
    if (m_current.isEmpty() || m_coverLogged.contains(m_current))
        return;
    const int saved = m_coverFromNet + m_coverFromFetch + m_coverFromClip;
    if (saved == 0 && m_coverFetched == 0 && m_coverError.isEmpty())
        return;
    m_coverLogged.insert(m_current);
    QString extra;
    if (!m_coverSample.isEmpty())
        extra += QStringLiteral(" amostra ") + m_coverSample.left(180);
    if (!m_coverError.isEmpty())
        extra += QStringLiteral(" erro ") + m_coverError.left(160);
    std::fprintf(stderr, "zappe: espelho %s capas %d salvas (rede %d, aba %d, recorte %d)%s\n", qPrintable(m_current),
                 saved, m_coverFromNet, m_coverFromFetch, m_coverFromClip, qPrintable(extra));
}

bool MirrorSession::fetchNextCoverBatch()
{
    if (m_player || !m_cdp || !m_cdp->connected())
        return false;
    m_coverFromNet += takeHeldCovers();
    if (m_coverFetchGaveUp || m_coverFetched >= 8) {
        if (m_coverFetchGaveUp)
            dropCurrentCoverJobs();
        return startCoverClips();
    }
    QVector<CoverJob> batch;
    QVector<CoverJob> rest;
    for (const CoverJob &job : m_covers) {
        if (batch.size() < 4 && job.serviceId == m_current && !m_coverAttempted.contains(job.url))
            batch.append(job);
        else
            rest.append(job);
    }
    if (batch.isEmpty())
        return startCoverClips();
    for (const CoverJob &job : batch)
        m_coverAttempted.insert(job.url);
    m_covers = rest;
    m_coverBatch = batch;
    m_coverFetched += int(batch.size());
    if (m_coverSample.isEmpty())
        m_coverSample = batch.first().url.left(160);
    QStringList urls;
    for (const CoverJob &job : batch)
        urls.append(job.url);
    const int gen = ++m_coverGen;
    m_coverPhase = 1;
    m_job = JobCovers;
    m_busy = true;
    m_cdp->evaluate(mirrorCoverFetchScript(urls), true);
    QTimer::singleShot(8000, this, [this, gen] { coverTimedOut(gen); });
    return true;
}

bool MirrorSession::startCoverClips()
{
    if (m_player || m_clipJobs.isEmpty() || !m_cdp || !m_cdp->connected())
        return false;
    if (m_clipJobs.size() > 6) {
        QVector<CoverJob> extra;
        while (m_clipJobs.size() > 6)
            extra.append(m_clipJobs.takeLast());
        if (!m_coverFetchGaveUp) {
            for (const CoverJob &job : extra)
                m_covers.append(job);
        }
    }
    QStringList urls;
    QStringList titles;
    for (const CoverJob &job : m_clipJobs) {
        urls.append(job.url);
        titles.append(job.title);
    }
    const int gen = ++m_coverGen;
    m_coverPhase = 2;
    m_job = JobCovers;
    m_busy = true;
    m_cdp->evaluate(mirrorCoverClipScript(urls, titles), false);
    QTimer::singleShot(12000, this, [this, gen] { coverTimedOut(gen); });
    return true;
}

void MirrorSession::shootNextClip()
{
    if (m_job != JobCovers || m_coverPhase != 3 || !m_cdp || !m_cdp->connected() || m_shotIndex >= m_shots.size()) {
        finishCoverPass();
        return;
    }
    const CoverShot &shot = m_shots.at(m_shotIndex);
    m_cdp->captureScreenshot(shot.x, shot.y, shot.w, shot.h);
}

void MirrorSession::coverTimedOut(int gen)
{
    if (m_job != JobCovers || m_coverGen != gen)
        return;
    ++m_coverGen;
    if (m_coverError.isEmpty())
        m_coverError = QStringLiteral("sem resposta da aba");
    if (m_coverPhase == 1) {
        for (const CoverJob &job : m_coverBatch)
            m_clipJobs.append(job);
        m_coverBatch.clear();
        m_coverFetchGaveUp = true;
        dropCurrentCoverJobs();
        if (startCoverClips())
            return;
    }
    for (const CoverJob &job : m_clipJobs) {
        if (!m_coverFetchGaveUp)
            m_covers.append(job);
    }
    m_clipJobs.clear();
    finishCoverPass();
}

void MirrorSession::finishCoverPass()
{
    if (m_coverClosed)
        return;
    m_coverClosed = true;
    m_coverPhase = 0;
    ++m_coverGen;
    m_coverBatch.clear();
    m_clipJobs.clear();
    m_shots.clear();
    logCoverSummary();
    finishAccept();
}

void MirrorSession::finishAccept()
{
    if (m_player)
        return;
    if (m_resumeExtra && !m_extraLeft.isEmpty() && m_cdp && m_cdp->connected()) {
        const QString next = m_extraLeft.takeFirst();
        m_mergePage = true;
        begin(m_current, next, JobExtract);
        return;
    }
    m_resumeExtra = false;
    m_mergePage = false;
    m_busy = false;
    m_job = JobNone;
    downloadNext();
    if (!m_cdp || !m_cdp->connected()) {
        schedulePump();
        return;
    }
    QTimer::singleShot(500, this, &MirrorSession::pump);
}

int MirrorSession::storeCover(const CoverJob &job, const QByteArray &body)
{
    QString detail;
    const QByteArray jpeg = coverJpegFromBody(body, &detail);
    if (jpeg.size() < 32)
        return -1;
    QDir().mkpath(QFileInfo(job.path).absolutePath());
    QFile out(job.path);
    if (!out.open(QIODevice::WriteOnly) || out.write(jpeg) != jpeg.size())
        return -1;
    QVector<ShelfData> shelves = m_pages.value(job.serviceId);
    bool changed = false;
    const int linked = assignCoverFile(shelves, job.url, job.path, &changed);
    if (changed && m_guide) {
        m_pages.insert(job.serviceId, shelves);
        m_guide->replaceServiceShelves(job.serviceId, shelves);
    }
    return linked;
}

int MirrorSession::saveFromNetwork(const CoverJob &job)
{
    const QByteArray body = heldImage(job.url);
    if (body.isEmpty())
        return 0;
    const int cards = storeCover(job, body);
    return cards < 0 ? 0 : cards;
}

void MirrorSession::attachHeldCover(const QString &url, const QByteArray &body)
{
    if (url.isEmpty() || body.isEmpty() || m_pages.isEmpty())
        return;
    for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it) {
        bool match = false;
        for (const ShelfData &shelf : it.value()) {
            for (const RowData &row : shelf.rows) {
                if (coverUrlsMatch(row.artworkSpec, url))
                    match = true;
            }
        }
        if (!match)
            continue;
        const QString path = coverCachePath(m_cacheDir, url);
        bool fresh = false;
        for (const ShelfData &shelf : it.value()) {
            for (const RowData &row : shelf.rows) {
                if (coverUrlsMatch(row.artworkSpec, url) && row.artwork == path)
                    fresh = true;
            }
        }
        if (fresh && coverIsFresh(path, QDateTime::currentDateTime(), mirrorCoverMaxAgeSecs()))
            continue;
        CoverJob job;
        job.serviceId = it.key();
        job.url = url;
        job.path = path;
        const int cards = storeCover(job, body);
        if (cards > 0) {
            std::fprintf(stderr, "zappe: espelho %s capa da rede cards %d %s\n", qPrintable(it.key()), cards,
                         qPrintable(fileUrl(path)));
        }
    }
}

void MirrorSession::startDirectCovers(const QVector<CoverJob> &jobs)
{
    m_directJobs = jobs;
    m_directActive = 0;
    m_directSaved = 0;
    m_directCards = 0;
    m_directFromNet = 0;
    m_directTotal = int(jobs.size());
    m_directError.clear();
    m_directFile.clear();
    logImageFormatsOnce();
    if (jobs.isEmpty()) {
        logDirectCovers();
        return;
    }
    pumpDirectCovers();
}

void MirrorSession::logDirectCovers()
{
    if (m_directTotal < 0 || m_current.isEmpty())
        return;
    QString extra;
    if (!m_directFile.isEmpty())
        extra += QStringLiteral(" ") + m_directFile;
    if (!m_directError.isEmpty())
        extra += QStringLiteral(" erro ") + m_directError.left(240);
    std::fprintf(stderr, "zappe: espelho %s capas %d/%d direto, cards %d, rede %d%s\n", qPrintable(m_current),
                 m_directSaved, m_directTotal, m_directCards, m_directFromNet, qPrintable(extra));
    m_directTotal = -1;
}

void MirrorSession::pumpDirectCovers()
{
    while (m_directActive < 4 && !m_directJobs.isEmpty()) {
        const CoverJob job = m_directJobs.takeFirst();
        ++m_directActive;
        QNetworkRequest request{QUrl(expandCoverUrl(job.url))};
        request.setHeader(QNetworkRequest::UserAgentHeader, chromeCatalogUserAgent());
        request.setRawHeader("Accept", "image/jpeg,image/png,image/webp;q=0.5,image/*;q=0.2,*/*;q=0.1");
        const QString referer = coverDownloadReferer(job.referer);
        if (!referer.isEmpty()) {
            request.setRawHeader("Referer", referer.toUtf8());
            const QUrl ref(referer);
            QUrl origin;
            origin.setScheme(ref.scheme());
            origin.setHost(ref.host());
            if (ref.port() > 0)
                origin.setPort(ref.port());
            request.setRawHeader("Origin", origin.toString(QUrl::FullyEncoded).toUtf8());
        }
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply *reply = m_http->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, job] {
            const QByteArray body = reply->readAll();
            const bool httpOk = reply->error() == QNetworkReply::NoError;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QString netErr = reply->errorString();
            const QString type = reply->header(QNetworkRequest::ContentTypeHeader).toString();
            reply->deleteLater();
            QString detail;
            const QByteArray jpeg = httpOk ? coverJpegFromBody(body, &detail, type) : QByteArray();
            if (jpeg.size() >= 32) {
                const int cards = storeCover(job, jpeg);
                if (cards >= 0) {
                    ++m_directSaved;
                    m_directCards += cards;
                    if (m_directFile.isEmpty())
                        m_directFile = fileUrl(job.path);
                }
            } else {
                const int netCards = saveFromNetwork(job);
                if (netCards > 0) {
                    m_directFromNet += netCards;
                    m_directCards += netCards;
                    if (m_directFile.isEmpty())
                        m_directFile = fileUrl(job.path);
                } else if (m_directError.isEmpty()) {
                    const QString mime = type.section(QLatin1Char(';'), 0, 0).trimmed();
                    const QString shown = expandCoverUrl(job.url).left(220);
                    std::fprintf(stderr, "zappe: espelho %s capa direta falhou http %d %s %s\n", qPrintable(job.serviceId),
                                 status, qPrintable(mime.isEmpty() ? QStringLiteral("-") : mime.left(40)),
                                 qPrintable(shown));
                    if (!httpOk && status <= 0)
                        m_directError = netErr.left(120);
                    else
                        m_directError = QStringLiteral("http %1 %2 %3")
                                            .arg(QString::number(status),
                                                 mime.isEmpty() ? QStringLiteral("-") : mime.left(40),
                                                 detail.isEmpty() ? coverBodyLabel(body) : detail);
                }
            }
            --m_directActive;
            if (m_directJobs.isEmpty() && m_directActive == 0)
                logDirectCovers();
            else
                pumpDirectCovers();
        });
    }
}

void MirrorSession::downloadNext()
{
    if (m_downloading || m_covers.isEmpty() || m_job == JobCovers)
        return;
    m_downloading = true;
    const CoverJob job = m_covers.takeFirst();
    QNetworkRequest request{QUrl(expandCoverUrl(job.url))};
    request.setHeader(QNetworkRequest::UserAgentHeader, chromeCatalogUserAgent());
    request.setRawHeader("Accept", "image/jpeg,image/png,image/webp;q=0.5,image/*;q=0.2,*/*;q=0.1");
    QString referer = coverDownloadReferer(job.referer);
    if (referer.isEmpty()) {
        const MirrorAdapter *adapter = findMirrorAdapter(m_adapters, job.serviceId);
        if (adapter)
            referer = coverDownloadReferer(adapter->homeUrl);
    }
    if (!referer.isEmpty())
        request.setRawHeader("Referer", referer.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_http->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, job] {
        const QByteArray body = reply->readAll();
        const bool httpOk = reply->error() == QNetworkReply::NoError;
        const QString type = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        reply->deleteLater();
        QString detail;
        const QByteArray jpeg = httpOk ? coverJpegFromBody(body, &detail, type) : QByteArray();
        if (jpeg.size() >= 32)
            storeCover(job, jpeg);
        else
            saveFromNetwork(job);
        m_downloading = false;
        downloadNext();
    });
}

} // namespace zappe
