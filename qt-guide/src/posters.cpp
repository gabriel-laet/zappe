#include "posters.h"

#include "util.h"

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace zappe {
namespace {

constexpr int kArtVersion = 1;

struct Palette {
    QColor a;
    QColor b;
    QColor c;
};

const Palette kPalettes[] = {
    {{0x1B, 0x3A, 0x4B}, {0xC4, 0x56, 0x2A}, {0xF4, 0xE1, 0xC1}},
    {{0x0E, 0x1A, 0x2B}, {0x3D, 0x7E, 0xA6}, {0xE8, 0xF1, 0xF8}},
    {{0x2A, 0x12, 0x1A}, {0xC4, 0x3B, 0x5A}, {0xF6, 0xD5, 0xC2}},
    {{0x12, 0x22, 0x18}, {0x3F, 0x8F, 0x6B}, {0xE4, 0xF0, 0xC8}},
    {{0x1A, 0x14, 0x2E}, {0x7A, 0x5C, 0xD6}, {0xE6, 0xD9, 0xFF}},
    {{0x2B, 0x1D, 0x10}, {0xD4, 0x8A, 0x3A}, {0xFB, 0xE6, 0xC8}},
    {{0x10, 0x18, 0x22}, {0x4C, 0x6E, 0x8A}, {0xD7, 0xE4, 0xEE}},
    {{0x24, 0x12, 0x28}, {0xE0, 0x6C, 0x9A}, {0xFF, 0xE0, 0xEC}},
    {{0x0F, 0x20, 0x22}, {0x1F, 0xA3, 0xA1}, {0xD4, 0xF4, 0xF2}},
    {{0x21, 0x18, 0x12}, {0xA8, 0x63, 0x3A}, {0xF3, 0xE2, 0xD0}},
    {{0x14, 0x16, 0x28}, {0xE0, 0x6A, 0x3A}, {0xFF, 0xD8, 0xC2}},
    {{0x1C, 0x14, 0x18}, {0x8E, 0x4A, 0x6A}, {0xF3, 0xD5, 0xC8}},
};

QString uiFamily()
{
    static const QString family = [] {
        const QStringList prefs = {QStringLiteral("Inter"), QStringLiteral("Noto Sans"),
                                   QStringLiteral("Noto Sans Display")};
        const QStringList families = QFontDatabase::families();
        for (const QString &pref : prefs) {
            if (families.contains(pref))
                return pref;
        }
        return QString();
    }();
    return family;
}

QString slug(const QString &title)
{
    QString text = title.toLower();
    text.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    while (text.startsWith(QLatin1Char('-')))
        text.remove(0, 1);
    while (text.endsWith(QLatin1Char('-')))
        text.chop(1);
    if (text.isEmpty())
        text = QStringLiteral("cover");
    if (text.size() > 42)
        text.truncate(42);
    return text;
}

QString artFile(const QString &cacheDir, const QString &key)
{
    QDir().mkpath(cacheDir);
    return QDir(cacheDir).filePath(key + QStringLiteral(".png"));
}

QImage boxBlur(const QImage &source, int radius, int passes)
{
    if (radius < 1 || source.isNull())
        return source;
    QImage in = source.convertToFormat(QImage::Format_RGB32);
    QImage tmp(in.size(), QImage::Format_RGB32);
    const int w = in.width();
    const int h = in.height();
    const int r = radius;

    auto horizontal = [&](const QImage &src, QImage &dst) {
        QVector<int> pr(w + 1), pg(w + 1), pb(w + 1);
        for (int y = 0; y < h; ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(src.constScanLine(y));
            pr[0] = pg[0] = pb[0] = 0;
            for (int x = 0; x < w; ++x) {
                pr[x + 1] = pr[x] + qRed(line[x]);
                pg[x + 1] = pg[x] + qGreen(line[x]);
                pb[x + 1] = pb[x] + qBlue(line[x]);
            }
            QRgb *out = reinterpret_cast<QRgb *>(dst.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const int x0 = std::max(0, x - r);
                const int x1 = std::min(w - 1, x + r);
                const int n = x1 - x0 + 1;
                out[x] = qRgb((pr[x1 + 1] - pr[x0]) / n, (pg[x1 + 1] - pg[x0]) / n, (pb[x1 + 1] - pb[x0]) / n);
            }
        }
    };
    auto vertical = [&](const QImage &src, QImage &dst) {
        QVector<int> pr((h + 1) * w), pg((h + 1) * w), pb((h + 1) * w);
        for (int y = 0; y < h; ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(src.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                const int i = (y + 1) * w + x;
                const int p = y * w + x;
                pr[i] = pr[p] + qRed(line[x]);
                pg[i] = pg[p] + qGreen(line[x]);
                pb[i] = pb[p] + qBlue(line[x]);
            }
        }
        for (int y = 0; y < h; ++y) {
            QRgb *out = reinterpret_cast<QRgb *>(dst.scanLine(y));
            const int y0 = std::max(0, y - r);
            const int y1 = std::min(h - 1, y + r);
            const int n = y1 - y0 + 1;
            for (int x = 0; x < w; ++x) {
                const int a = (y1 + 1) * w + x;
                const int b = y0 * w + x;
                out[x] = qRgb((pr[a] - pr[b]) / n, (pg[a] - pg[b]) / n, (pb[a] - pb[b]) / n);
            }
        }
    };

    QImage cur = in;
    for (int pass = 0; pass < passes; ++pass) {
        horizontal(cur, tmp);
        vertical(tmp, cur);
    }
    return cur;
}

QString hexColor(const QColor &color)
{
    return color.name(QColor::HexRgb);
}

QColor vivid(const Palette &pal)
{
    return pal.b.hslSaturation() >= pal.c.hslSaturation() ? pal.b : pal.c;
}

QStringList wrapLines(const QFontMetricsF &fm, const QString &text, qreal maxWidth, int maxLines)
{
    const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty())
        return {text};
    QStringList lines;
    QString current;
    int used = 0;
    for (int i = 0; i < words.size(); ++i) {
        const QString trial = current.isEmpty() ? words.at(i) : current + QLatin1Char(' ') + words.at(i);
        if (fm.horizontalAdvance(trial) <= maxWidth) {
            current = trial;
            ++used;
            continue;
        }
        if (!current.isEmpty())
            lines.push_back(current);
        else
            lines.push_back(fm.elidedText(words.at(i), Qt::ElideRight, maxWidth));
        current = words.at(i);
        ++used;
        if (lines.size() == maxLines) {
            current.clear();
            break;
        }
    }
    if (!current.isEmpty() && lines.size() < maxLines)
        lines.push_back(current);
    if (lines.size() == maxLines && used < words.size())
        lines.last() = fm.elidedText(lines.last() + QStringLiteral("…"), Qt::ElideRight, maxWidth);
    return lines;
}

void drawMotif(QPainter &p, const QRectF &rect, const QColor &color, quint32 hash, bool wide)
{
    QColor fill = color;
    fill.setAlpha(wide ? 150 : 180);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    switch (hash % 5) {
    case 0: {
        const qreal d = rect.height() * (wide ? 0.92 : 0.62);
        p.drawEllipse(QPointF(rect.width() * 0.78, rect.height() * 0.30), d * 0.5, d * 0.5);
        break;
    }
    case 1: {
        p.drawEllipse(QPointF(rect.width() * 0.30, rect.height() * 0.34), rect.width() * 0.26, rect.width() * 0.26);
        QColor second = color;
        second.setAlpha(90);
        p.setBrush(second);
        p.drawEllipse(QPointF(rect.width() * 0.74, rect.height() * 0.62), rect.width() * 0.16, rect.width() * 0.16);
        break;
    }
    case 2: {
        p.save();
        p.translate(rect.center());
        p.rotate(-16);
        p.drawRoundedRect(QRectF(-rect.width() * 0.12, -rect.height() * 0.72, rect.width() * 0.24, rect.height() * 1.44),
                          48, 48);
        p.restore();
        break;
    }
    case 3: {
        QPen pen(fill, wide ? 26.0 : 34.0);
        pen.setCapStyle(Qt::FlatCap);
        p.setBrush(Qt::NoBrush);
        p.setPen(pen);
        const qreal d = std::min(rect.width(), rect.height()) * 0.58;
        p.drawEllipse(QPointF(rect.width() * 0.52, rect.height() * (wide ? 0.42 : 0.36)), d / 2.0, d / 2.0);
        break;
    }
    default: {
        const qreal bar = rect.height() * (wide ? 0.055 : 0.032);
        qreal y = rect.height() * 0.18;
        for (int i = 0; i < 4; ++i) {
            const qreal w = rect.width() * (0.28 + ((hash >> (i * 3)) & 3) * 0.12);
            p.drawRoundedRect(QRectF(rect.width() * 0.12, y, w, bar), bar / 2.0, bar / 2.0);
            y += bar * 1.85;
        }
        break;
    }
    }
}

QImage renderPoster(const QString &title, const QString &serviceLabel, bool wide, const Palette &pal, quint32 hash)
{
    const int w = wide ? 960 : 640;
    const int h = wide ? 540 : 960;
    QImage image(w, h, QImage::Format_RGB32);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    const QRectF rect = image.rect();

    QLinearGradient bg(0, 0, wide ? rect.width() * 0.2 : 0, rect.height());
    if ((hash & 1u) != 0)
        bg.setFinalStop(rect.width(), rect.height() * 0.15);
    bg.setColorAt(0, pal.a);
    bg.setColorAt(1, pal.b);
    p.fillRect(rect, bg);

    QLinearGradient sheen(0, 0, 0, rect.height() * 0.28);
    sheen.setColorAt(0, QColor(255, 255, 255, 42));
    sheen.setColorAt(1, QColor(255, 255, 255, 0));
    p.fillRect(QRectF(0, 0, rect.width(), rect.height() * 0.28), sheen);

    drawMotif(p, rect, pal.c, hash, wide);

    QFont mono = QFont(uiFamily());
    mono.setPixelSize(wide ? 168 : 220);
    mono.setWeight(QFont::DemiBold);
    p.setFont(mono);
    p.setPen(QColor(255, 255, 255, 36));
    const QString letter = title.isEmpty() ? QStringLiteral("Z") : title.left(1).toUpper();
    p.drawText(QRectF(36, wide ? 8 : 36, rect.width() - 72, mono.pixelSize() + 8), Qt::AlignLeft | Qt::AlignVCenter,
               letter);

    QLinearGradient scrim(0, rect.height() * (wide ? 0.42 : 0.48), 0, rect.height());
    scrim.setColorAt(0, QColor(0, 0, 0, 0));
    scrim.setColorAt(1, QColor(0, 0, 0, 200));
    p.fillRect(rect, scrim);

    const qreal pad = wide ? 40.0 : 36.0;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 210));
    p.drawRoundedRect(QRectF(pad, rect.height() - (wide ? 118 : 168), 36, 4), 2, 2);

    QFont kicker(uiFamily());
    kicker.setPixelSize(wide ? 20 : 22);
    kicker.setWeight(QFont::Medium);
    kicker.setLetterSpacing(QFont::AbsoluteSpacing, 2.4);
    p.setFont(kicker);
    p.setPen(QColor(255, 255, 255, 210));
    const QString kickerText = serviceLabel.toUpper();
    p.drawText(QRectF(pad, rect.height() - (wide ? 108 : 154), rect.width() - pad * 2, 28),
               Qt::AlignLeft | Qt::AlignVCenter, kickerText);

    QFont titleFont(uiFamily());
    titleFont.setPixelSize(wide ? 48 : 46);
    titleFont.setWeight(QFont::DemiBold);
    titleFont.setLetterSpacing(QFont::AbsoluteSpacing, -0.6);
    p.setFont(titleFont);
    p.setPen(Qt::white);
    const QFontMetricsF metrics(titleFont);
    const QStringList lines = wrapLines(metrics, title, rect.width() - pad * 2, wide ? 2 : 3);
    const qreal lineH = metrics.height();
    qreal y = rect.height() - pad - lines.size() * lineH + metrics.ascent() * 0.15;
    for (const QString &line : lines) {
        p.drawText(QPointF(pad, y + metrics.ascent()), line);
        y += lineH;
    }
    return image;
}

QImage ambientFrom(const QImage &poster)
{
    const QImage small = poster.scaled(140, 78, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const QImage blurred = boxBlur(small, 7, 2);
    return blurred.scaled(960, 540, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QString averageHex(const QImage &image)
{
    const QImage sample = image.scaled(16, 16, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                              .convertToFormat(QImage::Format_ARGB32);
    qint64 r = 0;
    qint64 g = 0;
    qint64 b = 0;
    qint64 n = 0;
    for (int y = 0; y < sample.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(sample.constScanLine(y));
        for (int x = 0; x < sample.width(); ++x) {
            if (qAlpha(line[x]) < 16)
                continue;
            r += qRed(line[x]);
            g += qGreen(line[x]);
            b += qBlue(line[x]);
            ++n;
        }
    }
    if (n == 0)
        return QStringLiteral("#1c1c1e");
    return hexColor(QColor(int(r / n), int(g / n), int(b / n)));
}

ArtPaths finish(const QString &cacheDir, const QString &key, const QImage &poster, const QString &color)
{
    ArtPaths paths;
    paths.artwork = artFile(cacheDir, key);
    paths.ambient = artFile(cacheDir, key + QStringLiteral("-ambient"));
    paths.color = color;
    if (!QFileInfo::exists(paths.artwork))
        poster.save(paths.artwork, "PNG");
    if (!QFileInfo::exists(paths.ambient))
        ambientFrom(poster).save(paths.ambient, "PNG");
    return paths;
}

} // namespace

ArtPaths makePoster(const QString &cacheDir, const QString &title, const QString &serviceLabel, bool wide)
{
    const quint32 hash = fnv1a(title + QStringLiteral("|") + serviceLabel);
    const Palette pal = kPalettes[hash % (sizeof(kPalettes) / sizeof(kPalettes[0]))];
    const QString key = QStringLiteral("v%1-%2-%3").arg(kArtVersion).arg(wide ? "wide" : "poster").arg(slug(title));
    const QString artwork = artFile(cacheDir, key);
    const QString ambient = artFile(cacheDir, key + QStringLiteral("-ambient"));
    if (QFileInfo::exists(artwork) && QFileInfo::exists(ambient)) {
        ArtPaths cached;
        cached.artwork = artwork;
        cached.ambient = ambient;
        cached.color = hexColor(vivid(pal));
        return cached;
    }
    const QImage poster = renderPoster(title, serviceLabel, wide, pal, hash);
    return finish(cacheDir, key, poster, hexColor(vivid(pal)));
}

ArtPaths artFromFile(const QString &cacheDir, const QString &imagePath, const QString &title, const QString &serviceLabel,
                     bool wide)
{
    Q_UNUSED(serviceLabel);
    const QFileInfo info(imagePath);
    const QString key = QStringLiteral("v%1-file-%2-%3-%4")
                            .arg(kArtVersion)
                            .arg(slug(title))
                            .arg(info.size())
                            .arg(info.lastModified().toSecsSinceEpoch());
    const QString artwork = artFile(cacheDir, key);
    const QString ambient = artFile(cacheDir, key + QStringLiteral("-ambient"));
    QImage poster(imagePath);
    if (poster.isNull())
        return makePoster(cacheDir, title, serviceLabel, wide);
    poster = poster.convertToFormat(QImage::Format_RGB32);
    if (QFileInfo::exists(artwork) && QFileInfo::exists(ambient)) {
        ArtPaths cached;
        cached.artwork = artwork;
        cached.ambient = ambient;
        cached.color = averageHex(poster);
        return cached;
    }
    const QImage scaled = poster.scaled(wide ? 960 : 640, wide ? 540 : 960, Qt::KeepAspectRatioByExpanding,
                                        Qt::SmoothTransformation);
    const int x = std::max(0, (scaled.width() - (wide ? 960 : 640)) / 2);
    const int y = std::max(0, (scaled.height() - (wide ? 540 : 960)) / 2);
    const QImage cropped = scaled.copy(x, y, wide ? 960 : 640, wide ? 540 : 960);
    return finish(cacheDir, key, cropped, averageHex(cropped));
}

QString makeShadowSprite(const QString &cacheDir)
{
    const QString path = artFile(cacheDir, QStringLiteral("v%1-shadow").arg(kArtVersion));
    if (QFileInfo::exists(path))
        return path;
    // Paint the shape white on black, blur, then use the luminance as the shadow alpha.
    QImage white(480, 280, QImage::Format_RGB32);
    white.fill(Qt::black);
    QPainter painter(&white);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawRoundedRect(QRectF(78, 40, 324, 140), 26, 26);
    painter.end();
    const QImage soft = boxBlur(white, 16, 3);
    QImage shadow(soft.size(), QImage::Format_ARGB32);
    for (int y = 0; y < soft.height(); ++y) {
        const QRgb *src = reinterpret_cast<const QRgb *>(soft.constScanLine(y));
        QRgb *dst = reinterpret_cast<QRgb *>(shadow.scanLine(y));
        for (int x = 0; x < soft.width(); ++x)
            dst[x] = qRgba(0, 0, 0, std::min(200, qRed(src[x])));
    }
    shadow.save(path, "PNG");
    return path;
}

QString makeGlowSprite(const QString &cacheDir, const QColor &accent)
{
    const QString key = QStringLiteral("v%1-glow-%2").arg(kArtVersion).arg(accent.name(QColor::HexRgb).mid(1));
    const QString path = artFile(cacheDir, key);
    if (QFileInfo::exists(path))
        return path;
    const int s = 360;
    QImage image(s, s, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QRadialGradient gradient(s / 2.0, s / 2.0, s / 2.0);
    QColor mid = accent;
    mid.setAlpha(120);
    QColor edge = accent;
    edge.setAlpha(0);
    QColor core = accent;
    core.setAlpha(90);
    gradient.setColorAt(0.0, core);
    gradient.setColorAt(0.42, mid);
    gradient.setColorAt(1.0, edge);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(image.rect(), gradient);
    p.end();
    image.save(path, "PNG");
    return path;
}

QString makeCircleMark(const QString &cacheDir, const QString &imagePath, int size)
{
    if (imagePath.isEmpty() || size < 8)
        return {};
    const QFileInfo info(imagePath);
    const QString key = QStringLiteral("v%1-mark-%2-%3-%4")
                            .arg(kArtVersion)
                            .arg(size)
                            .arg(info.size())
                            .arg(info.lastModified().toSecsSinceEpoch());
    const QString path = artFile(cacheDir, key);
    if (QFileInfo::exists(path))
        return path;
    QImage source(imagePath);
    if (source.isNull())
        return {};
    QImage out(size, size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addEllipse(0, 0, size, size);
    p.setClipPath(clip);
    p.drawImage(QRect(0, 0, size, size), source);
    p.end();
    out.save(path, "PNG");
    return path;
}

} // namespace zappe
