#pragma once

#include "catalog.h"

#include <QColor>
#include <QString>

namespace zappe {

struct ArtPaths {
    QString artwork;
    QString ambient;
    QString color;
    // Monograma ou arquivo da rede. O card não cobre isso com o granulado.
    bool mark = false;
};

ArtPaths makePoster(const QString &cacheDir, const QString &title, const QString &serviceLabel, bool wide);
// Canal ao vivo sem frame: nome e degradê, sem o desenho genérico da capa.
ArtPaths makeChannelCard(const QString &cacheDir, const QString &name);
// Id estável da rede (globo, sbt, record, band, cultura, redetv, tvbrasil) ou vazio.
QString networkMarkId(const QString &name);
// Nome do SDT e, se ele não citar a rede, o virtual de São Paulo (5.1 Globo, 4.1 SBT…).
// 3.1 (NGT) e os minors diferentes de 1 ficam de fora.
QString networkMarkFor(const QString &name, const QString &number);
// Monograma original nas cores públicas da rede, um arquivo por rede. Vazio se a rede é desconhecida.
ArtPaths makeNetworkLogo(const QString &cacheDir, const QString &name);
// Logo da rede quando existe. Procura logos/{id}.png no diretório de dados; senão o monograma.
// Sem marca, o card com nome e cor.
ArtPaths liveChannelArt(const QString &cacheDir, const QString &name, const QString &dataDir = {},
                        const QString &number = {});
ArtPaths artFromFile(const QString &cacheDir, const QString &imagePath, const QString &title, const QString &serviceLabel,
                     bool wide);
QString makeShadowSprite(const QString &cacheDir);
QString makeGlowSprite(const QString &cacheDir, const QColor &accent);
QString makeCircleMark(const QString &cacheDir, const QString &imagePath, int size);
QString makeNoiseSprite(const QString &cacheDir);

} // namespace zappe
