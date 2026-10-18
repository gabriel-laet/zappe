#pragma once

#include <QSize>
#include <QString>

namespace zappe {

// Padrão 1080p. "4K" é a única outra opção e vale no próximo início do kiosk.
QString normalizeResolution(const QString &raw);
QSize resolutionSize(const QString &resolution);
QString loadResolution(const QString &dataDir);
bool saveResolution(const QString &dataDir, const QString &resolution);

} // namespace zappe
