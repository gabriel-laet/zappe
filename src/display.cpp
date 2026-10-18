#include "display.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace zappe {

QString normalizeResolution(const QString &raw)
{
    if (raw.trimmed().compare(QLatin1String("4k"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("4K");
    return QStringLiteral("1080p");
}

QSize resolutionSize(const QString &resolution)
{
    if (normalizeResolution(resolution) == QLatin1String("4K"))
        return {3840, 2160};
    return {1920, 1080};
}

QString loadResolution(const QString &dataDir)
{
    QFile file(QDir(dataDir).filePath(QStringLiteral("display.json")));
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("1080p");
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return QStringLiteral("1080p");
    return normalizeResolution(doc.object().value(QStringLiteral("resolution")).toString());
}

bool saveResolution(const QString &dataDir, const QString &resolution)
{
    QDir().mkpath(dataDir);
    QSaveFile file(QDir(dataDir).filePath(QStringLiteral("display.json")));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QJsonObject obj;
    obj.insert(QStringLiteral("resolution"), normalizeResolution(resolution));
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return file.commit();
}

} // namespace zappe
