#include "catalog.h"

#include "posters.h"
#include "util.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>
#include <QVariant>

#include <cstdio>

namespace zappe {
namespace {

QString optString(const QJsonObject &obj, const char *key)
{
    const QJsonValue value = obj.value(QLatin1String(key));
    return value.isString() ? value.toString() : QString();
}

QString labelize(const QString &service)
{
    QString text = service.trimmed();
    if (text.isEmpty())
        return QStringLiteral("Library");
    if (text.compare(QLatin1String("live"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Ao vivo");
    text.replace(QLatin1Char('_'), QLatin1Char(' '));
    text.replace(QLatin1Char('-'), QLatin1Char(' '));
    QStringList parts;
    for (const QString &part : text.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        QString word = part.toLower();
        word[0] = word[0].toUpper();
        parts.push_back(word);
    }
    return parts.join(QLatin1Char(' '));
}

bool shelfIsWide(const QString &id)
{
    const QString key = id.trimmed().toLower();
    return key == QLatin1String("continue") || key == QLatin1String("live") || key == QLatin1String("apps")
        || key == QLatin1String("canais") || key == QLatin1String("ao-vivo") || key == QLatin1String("aovivo");
}

bool shelfIsLive(const QString &id)
{
    const QString key = id.trimmed().toLower();
    return key == QLatin1String("live") || key == QLatin1String("ao-vivo") || key == QLatin1String("aovivo");
}

QString resolveArtwork(const QString &spec, const QString &catalogPath, const QString &dataDir)
{
    const QString trimmed = spec.trimmed();
    if (trimmed.isEmpty())
        return {};
    QString path = trimmed;
    if (path.startsWith(QStringLiteral("file:")))
        path = QUrl(path).toLocalFile();
    const QFileInfo info(path);
    const QStringList candidates = {
        info.isAbsolute() ? info.absoluteFilePath() : QString(),
        QFileInfo(catalogPath).dir().filePath(path),
        QDir(dataDir).filePath(path),
    };
    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty() && QFileInfo(candidate).isFile())
            return QFileInfo(candidate).absoluteFilePath();
    }
    std::fprintf(stderr, "zappe: artwork missing (%s); using a generated cover\n", qPrintable(trimmed));
    return {};
}

} // namespace

RowModel::RowModel(QVector<RowData> rows, QObject *parent)
    : QAbstractListModel(parent)
    , m_rows(std::move(rows))
{
}

int RowModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant RowModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const RowData &row = m_rows.at(index.row());
    switch (role) {
    case TitleRole:
        return row.title;
    case ServiceRole:
        return row.serviceLabel;
    case HrefRole:
        return row.href;
    case SkillRole:
        return row.skillId;
    case ArtworkRole:
        return fileUrl(row.artwork);
    case AmbientRole:
        return fileUrl(row.ambient);
    case ColorRole:
        return row.color;
    case WideRole:
        return row.wide;
    case LiveRole:
        return row.live;
    case FrameRole:
        return row.frame;
    case MarkRole:
        return row.mark;
    case ProgressRole:
        return row.progress;
    default:
        return {};
    }
}

QHash<int, QByteArray> RowModel::roleNames() const
{
    return {
        {TitleRole, "title"},
        {ServiceRole, "service"},
        {HrefRole, "href"},
        {SkillRole, "skillId"},
        {ArtworkRole, "artwork"},
        {AmbientRole, "ambient"},
        {ColorRole, "color"},
        {WideRole, "wide"},
        {LiveRole, "live"},
        {FrameRole, "frame"},
        {MarkRole, "mark"},
        {ProgressRole, "progress"},
    };
}

const RowData *RowModel::at(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return nullptr;
    return &m_rows.at(row);
}

ShelfModel::ShelfModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void ShelfModel::setShelves(QVector<ShelfData> shelves)
{
    beginResetModel();
    for (Entry &entry : m_shelves)
        delete entry.rows;
    m_shelves.clear();
    m_shelves.reserve(shelves.size());
    for (ShelfData &shelf : shelves) {
        Entry entry;
        entry.data = shelf;
        entry.rows = new RowModel(shelf.rows, this);
        m_shelves.push_back(entry);
    }
    endResetModel();
}

int ShelfModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_shelves.size();
}

QVariant ShelfModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_shelves.size())
        return {};
    const Entry &entry = m_shelves.at(index.row());
    switch (role) {
    case ShelfIdRole:
        return entry.data.id;
    case TitleRole:
        return entry.data.title;
    case StatusRole:
        return entry.data.status;
    case MessageRole:
        return entry.data.message;
    case SkillRole:
        return entry.data.skillId;
    case WideRole:
        return entry.data.rows.isEmpty() ? false : entry.data.rows.first().wide;
    case RowsRole:
        return QVariant::fromValue(static_cast<QObject *>(entry.rows));
    default:
        return {};
    }
}

QHash<int, QByteArray> ShelfModel::roleNames() const
{
    return {
        {ShelfIdRole, "shelfId"},
        {TitleRole, "title"},
        {StatusRole, "status"},
        {MessageRole, "message"},
        {SkillRole, "skillId"},
        {WideRole, "wide"},
        {RowsRole, "rows"},
    };
}

const ShelfData *ShelfModel::shelfAt(int row) const
{
    if (row < 0 || row >= m_shelves.size())
        return nullptr;
    return &m_shelves.at(row).data;
}

RowModel *ShelfModel::rowsAt(int row) const
{
    if (row < 0 || row >= m_shelves.size())
        return nullptr;
    return m_shelves.at(row).rows;
}

QString resolveCatalogPath(const QString &explicitPath, const QString &dataDir)
{
    if (!explicitPath.isEmpty())
        return explicitPath;
    if (const QString env = qEnvironmentVariable("ZAPPE_CATALOG"); !env.isEmpty())
        return env;
    const QString user = QDir(dataDir).filePath(QStringLiteral("catalog.json"));
    if (QFileInfo::exists(user))
        return user;
    const QString beside = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("catalog.json"));
    if (QFileInfo::exists(beside))
        return beside;
    return QStringLiteral(":/zappe/data/catalog.json");
}

bool harvestErrorText(const QString &text)
{
    const QString folded = text.toLower();
    return folded.contains(QStringLiteral("at-spi")) || folded.contains(QStringLiteral("no chrome/chromium"))
           || folded.contains(QStringLiteral("org.a11y")) || folded.contains(QStringLiteral("accessibility is off"));
}

bool removeHarvestErrors(QJsonObject &root)
{
    const QJsonArray shelves = root.value(QStringLiteral("shelves")).toArray();
    QJsonArray kept;
    bool changed = false;
    for (const QJsonValue &value : shelves) {
        if (!value.isObject()) {
            changed = true;
            continue;
        }
        QJsonObject shelf = value.toObject();
        const QString status = shelf.value(QStringLiteral("status")).toString().trimmed().toLower();
        if (status == QLatin1String("error") || status == QLatin1String("teach")
            || status == QLatin1String("harvesting") || harvestErrorText(shelf.value(QStringLiteral("message")).toString())
            || harvestErrorText(shelf.value(QStringLiteral("title")).toString())) {
            changed = true;
            continue;
        }
        const QJsonArray rows = shelf.value(QStringLiteral("rows")).toArray();
        QJsonArray cleanRows;
        bool droppedRow = false;
        for (const QJsonValue &rowValue : rows) {
            const QString title = rowValue.toObject().value(QStringLiteral("title")).toString();
            if (harvestErrorText(title)) {
                droppedRow = true;
                continue;
            }
            cleanRows.append(rowValue);
        }
        if (droppedRow) {
            shelf.insert(QStringLiteral("rows"), cleanRows);
            changed = true;
        }
        kept.append(shelf);
    }
    if (changed)
        root.insert(QStringLiteral("shelves"), kept);
    return changed;
}

CatalogFile loadCatalogFile(const QString &path)
{
    CatalogFile catalog;
    catalog.path = path;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "zappe: could not read catalog %s\n", qPrintable(path));
        return catalog;
    }
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        std::fprintf(stderr, "zappe: invalid catalog JSON (%s)\n", qPrintable(error.errorString()));
        return catalog;
    }
    QJsonObject root = doc.object();
    if (removeHarvestErrors(root) && !path.startsWith(QLatin1String(":/"))) {
        QSaveFile out(path);
        if (out.open(QIODevice::WriteOnly)) {
            out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            if (!out.commit())
                std::fprintf(stderr, "zappe: não consegui limpar o catálogo %s\n", qPrintable(path));
        }
    }
    catalog.ok = true;
    const QJsonArray shelves = root.value(QStringLiteral("shelves")).toArray();
    for (const QJsonValue &shelfVal : shelves) {
        if (!shelfVal.isObject())
            continue;
        const QJsonObject shelfObj = shelfVal.toObject();
        ShelfData shelf;
        shelf.id = optString(shelfObj, "id");
        shelf.skillId = optString(shelfObj, "skill_id");
        shelf.title = optString(shelfObj, "title");
        if (shelf.title.isEmpty())
            shelf.title = shelf.id.isEmpty() ? QStringLiteral("Shelf") : shelf.id;
        shelf.status = optString(shelfObj, "status");
        if (shelf.status.isEmpty())
            shelf.status = QStringLiteral("empty");
        shelf.message = optString(shelfObj, "message");
        shelf.live = shelfIsLive(shelf.id);
        const bool wide = shelfIsWide(shelf.id);
        const QJsonArray rows = shelfObj.value(QStringLiteral("rows")).toArray();
        for (const QJsonValue &rowVal : rows) {
            if (!rowVal.isObject())
                continue;
            const QJsonObject rowObj = rowVal.toObject();
            RowData row;
            row.title = optString(rowObj, "title").trimmed();
            if (row.title.isEmpty())
                continue;
            row.service = optString(rowObj, "service");
            row.serviceLabel = labelize(row.service);
            row.href = optString(rowObj, "href");
            row.skillId = optString(rowObj, "skill_id");
            if (row.skillId.isEmpty())
                row.skillId = shelf.skillId;
            row.artworkSpec = optString(rowObj, "artwork");
            row.wide = wide;
            row.live = shelf.live;
            if (wide && !row.live) {
                const quint32 hash = fnv1a(row.title);
                row.progress = 0.12 + (hash % 73) / 100.0;
            }
            shelf.rows.push_back(row);
        }
        catalog.shelves.push_back(shelf);
    }
    return catalog;
}

void decorateCatalog(CatalogFile &catalog, const QString &dataDir, const QString &cacheDir)
{
    for (ShelfData &shelf : catalog.shelves) {
        for (RowData &row : shelf.rows) {
            const QString artwork = resolveArtwork(row.artworkSpec, catalog.path, dataDir);
            const ArtPaths art = !artwork.isEmpty()
                                     ? artFromFile(cacheDir, artwork, row.title, row.serviceLabel, row.wide)
                                     : (row.live ? liveChannelArt(cacheDir, row.title, dataDir, row.number)
                                                 : makePoster(cacheDir, row.title, row.serviceLabel, row.wide));
            row.artwork = art.artwork;
            row.ambient = art.ambient;
            row.color = art.color;
            row.mark = row.live && !row.frame && art.mark;
        }
    }
}

} // namespace zappe
