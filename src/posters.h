#pragma once

#include "catalog.h"

#include <QColor>
#include <QString>

namespace zappe {

struct ArtPaths {
    QString artwork;
    QString ambient;
    QString color;
};

ArtPaths makePoster(const QString &cacheDir, const QString &title, const QString &serviceLabel, bool wide);
ArtPaths artFromFile(const QString &cacheDir, const QString &imagePath, const QString &title, const QString &serviceLabel,
                     bool wide);
QString makeShadowSprite(const QString &cacheDir);
QString makeGlowSprite(const QString &cacheDir, const QColor &accent);
QString makeCircleMark(const QString &cacheDir, const QString &imagePath, int size);
QString makeNoiseSprite(const QString &cacheDir);

} // namespace zappe
