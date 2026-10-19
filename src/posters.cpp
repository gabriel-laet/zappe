#include "posters.h"

#include "util.h"

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QSet>
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
#include <cstdio>

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

QImage renderChannel(const QString &name, const Palette &pal)
{
    QImage image(960, 540, QImage::Format_RGB32);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    const QRectF rect = image.rect();

    QLinearGradient bg(0, 0, rect.width(), rect.height());
    bg.setColorAt(0, pal.a);
    bg.setColorAt(0.55, pal.b);
    bg.setColorAt(1, pal.a.darker(160));
    p.fillRect(rect, bg);

    QRadialGradient glow(rect.width() * 0.72, rect.height() * 0.28, rect.width() * 0.55);
    QColor glowColor = pal.c;
    glowColor.setAlpha(70);
    glow.setColorAt(0, glowColor);
    glow.setColorAt(1, QColor(0, 0, 0, 0));
    p.fillRect(rect, glow);

    QFont kicker(uiFamily());
    kicker.setPixelSize(22);
    kicker.setWeight(QFont::DemiBold);
    kicker.setLetterSpacing(QFont::AbsoluteSpacing, 2.2);
    p.setFont(kicker);
    p.setPen(QColor(255, 255, 255, 220));
    p.drawText(QRectF(48, 48, rect.width() - 96, 32), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("AO VIVO"));

    QFont titleFont(uiFamily());
    titleFont.setPixelSize(72);
    titleFont.setWeight(QFont::DemiBold);
    titleFont.setLetterSpacing(QFont::AbsoluteSpacing, -1.1);
    p.setFont(titleFont);
    p.setPen(Qt::white);
    const QFontMetricsF metrics(titleFont);
    const QStringList lines = wrapLines(metrics, name, rect.width() - 96, 2);
    const qreal lineH = metrics.height();
    qreal y = rect.center().y() - lines.size() * lineH * 0.5 + metrics.ascent() * 0.15;
    for (const QString &line : lines) {
        p.drawText(QPointF(48, y + metrics.ascent()), line);
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

ArtPaths makeChannelCard(const QString &cacheDir, const QString &name)
{
    const quint32 hash = fnv1a(name + QStringLiteral("|channel"));
    const Palette pal = kPalettes[hash % (sizeof(kPalettes) / sizeof(kPalettes[0]))];
    const QString key = QStringLiteral("v%1-channel-%2").arg(kArtVersion).arg(slug(name));
    const QString artwork = artFile(cacheDir, key);
    const QString ambient = artFile(cacheDir, key + QStringLiteral("-ambient"));
    if (QFileInfo::exists(artwork) && QFileInfo::exists(ambient)) {
        ArtPaths cached;
        cached.artwork = artwork;
        cached.ambient = ambient;
        cached.color = hexColor(vivid(pal));
        return cached;
    }
    return finish(cacheDir, key, renderChannel(name, pal), hexColor(vivid(pal)));
}

QString networkMarkId(const QString &name)
{
    QString folded;
    for (const QChar ch : name) {
        switch (ch.unicode()) {
        case u'Á':
        case u'À':
        case u'Ã':
        case u'Â':
        case u'á':
        case u'à':
        case u'ã':
        case u'â':
            folded.append(QLatin1Char('a'));
            break;
        case u'É':
        case u'Ê':
        case u'é':
        case u'ê':
            folded.append(QLatin1Char('e'));
            break;
        case u'Í':
        case u'í':
            folded.append(QLatin1Char('i'));
            break;
        case u'Ó':
        case u'Ô':
        case u'Õ':
        case u'ó':
        case u'ô':
        case u'õ':
            folded.append(QLatin1Char('o'));
            break;
        case u'Ú':
        case u'ú':
            folded.append(QLatin1Char('u'));
            break;
        case u'Ç':
        case u'ç':
            folded.append(QLatin1Char('c'));
            break;
        default:
            folded.append(ch.toLower());
            break;
        }
    }
    folded = folded.simplified();
    if (folded.contains(QStringLiteral("redetv")) || folded.contains(QStringLiteral("rede tv")))
        return QStringLiteral("redetv");
    if (folded.contains(QStringLiteral("bandeirantes")) || folded.contains(QStringLiteral("band")))
        return QStringLiteral("band");
    if (folded.contains(QStringLiteral("record")))
        return QStringLiteral("record");
    if (folded.contains(QStringLiteral("globo")))
        return QStringLiteral("globo");
    if (folded.contains(QStringLiteral("sbt")))
        return QStringLiteral("sbt");
    if (folded.contains(QStringLiteral("cultura")))
        return QStringLiteral("cultura");
    if (folded.contains(QStringLiteral("tv brasil")) || folded.contains(QStringLiteral("tvbrasil")))
        return QStringLiteral("tvbrasil");
    if (folded.contains(QStringLiteral("novo tempo")) || folded.contains(QStringLiteral("novotempo")))
        return QStringLiteral("novotempo");
    if (folded.contains(QStringLiteral("gazeta")))
        return QStringLiteral("gazeta");
    if (folded.contains(QStringLiteral("ngt")))
        return QStringLiteral("ngt");
    return {};
}

QString networkMarkFor(const QString &name, const QString &number)
{
    const QString byName = networkMarkId(name);
    if (!byName.isEmpty())
        return byName;
    static const QRegularExpression re(QStringLiteral("^(\\d{1,3})(?:\\.(\\d))?$"));
    const QRegularExpressionMatch match = re.match(number.trimmed());
    if (!match.hasMatch())
        return {};
    const int minor = match.captured(2).isEmpty() ? -1 : match.captured(2).toInt();
    if (!(minor < 0 || minor == 1))
        return {};
    switch (match.captured(1).toInt()) {
    case 5:
        return QStringLiteral("globo");
    case 4:
        return QStringLiteral("sbt");
    case 7:
        return QStringLiteral("record");
    case 13:
        return QStringLiteral("band");
    case 2:
        return QStringLiteral("cultura");
    case 9:
        return QStringLiteral("redetv");
    case 11:
        return QStringLiteral("gazeta");
    case 3:
        return QStringLiteral("ngt");
    default:
        return {};
    }
}

namespace {

struct NetworkMark {
    const char *id;
    const char *label;
    const char *color;
};

const NetworkMark *networkMark(const QString &id)
{
    static const NetworkMark marks[] = {
        {"globo", "GLOBO", "#2457A6"},         {"sbt", "SBT", "#E10600"},
        {"record", "RECORD", "#B00020"},       {"band", "BAND", "#D71920"},
        {"cultura", "CULTURA", "#0E7A3D"},     {"redetv", "REDETV", "#F15A22"},
        {"tvbrasil", "TV BRASIL", "#0055A5"},  {"gazeta", "GAZETA", "#6B2D8B"},
        {"ngt", "NGT", "#0B6E4F"},             {"novotempo", "NOVO TEMPO", "#1A4F8B"},
    };
    for (const NetworkMark &item : marks) {
        if (id == QLatin1String(item.id))
            return &item;
    }
    return nullptr;
}

QString findNetworkAsset(const QString &dataDir, const QString &id)
{
    if (dataDir.isEmpty() || id.isEmpty())
        return {};
    const QStringList dirs = {QDir(dataDir).filePath(QStringLiteral("logos")),
                              QDir(dataDir).filePath(QStringLiteral("ota-logos"))};
    const QStringList exts = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                              QStringLiteral("webp")};
    for (const QString &dir : dirs) {
        for (const QString &ext : exts) {
            const QString path = QDir(dir).filePath(id + QLatin1Char('.') + ext);
            if (QFileInfo::exists(path))
                return path;
        }
    }
    return {};
}

QString ensureNetworkLogoFile(const QString &dataDir, const QString &id)
{
    const QString existing = findNetworkAsset(dataDir, id);
    if (!existing.isEmpty())
        return existing;
    const NetworkMark *mark = networkMark(id);
    if (!mark)
        return {};
    const QString dir = QDir(dataDir).filePath(QStringLiteral("logos"));
    if (!QDir().mkpath(dir))
        return {};
    const QString path = QDir(dir).filePath(id + QStringLiteral(".png"));
    QImage image(880, 360, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    QFont font(uiFamily());
    const QString label = QString::fromLatin1(mark->label);
    font.setPixelSize(label.size() > 8 ? 72 : 120);
    font.setWeight(QFont::Black);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 2);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(image.rect().adjusted(24, 0, -24, 0), Qt::AlignCenter, label);
    painter.end();
    if (!image.save(path, "PNG"))
        return {};
    return path;
}

ArtPaths paintNetworkMark(const QString &cacheDir, const NetworkMark &mark, const QString &assetPath)
{
    QString key = QStringLiteral("v%1-net-%2").arg(kArtVersion).arg(QString::fromLatin1(mark.id));
    if (!assetPath.isEmpty()) {
        const QFileInfo info(assetPath);
        key = QStringLiteral("v%1-netfile-%2-%3-%4")
                  .arg(kArtVersion)
                  .arg(QString::fromLatin1(mark.id))
                  .arg(info.size())
                  .arg(info.lastModified().toSecsSinceEpoch());
    }
    const QColor color(QString::fromLatin1(mark.color));
    const QString artwork = artFile(cacheDir, key);
    const QString ambient = artFile(cacheDir, key + QStringLiteral("-ambient"));
    if (QFileInfo::exists(artwork) && QFileInfo::exists(ambient)) {
        ArtPaths cached;
        cached.artwork = artwork;
        cached.ambient = ambient;
        cached.color = hexColor(color);
        cached.mark = true;
        return cached;
    }
    QImage image(960, 540, QImage::Format_RGB32);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.fillRect(image.rect(), color);
    QImage logo;
    if (!assetPath.isEmpty())
        logo.load(assetPath);
    if (!logo.isNull()) {
        const QImage scaled = logo.scaled(800, 420, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter.drawImage((image.width() - scaled.width()) / 2, (image.height() - scaled.height()) / 2, scaled);
    } else {
        QColor disc = color.lighter(125);
        painter.setPen(Qt::NoPen);
        painter.setBrush(disc);
        painter.drawEllipse(QPointF(720, 150), 180, 180);
        painter.setBrush(color.darker(140));
        painter.drawRoundedRect(QRectF(72, 168, 420, 220), 36, 36);
        QFont font(uiFamily());
        font.setPixelSize(qstrlen(mark.label) > 8 ? 64 : 92);
        font.setWeight(QFont::Black);
        font.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(image.rect().adjusted(48, 0, -48, 0), Qt::AlignCenter, QString::fromLatin1(mark.label));
    }
    painter.end();
    ArtPaths painted = finish(cacheDir, key, image, hexColor(color));
    painted.mark = !painted.artwork.isEmpty();
    return painted;
}

} // namespace

ArtPaths makeNetworkLogo(const QString &cacheDir, const QString &name)
{
    const NetworkMark *mark = networkMark(networkMarkId(name));
    if (!mark)
        return {};
    return paintNetworkMark(cacheDir, *mark, {});
}

ArtPaths liveChannelArt(const QString &cacheDir, const QString &name, const QString &dataDir, const QString &number)
{
    const QString id = networkMarkFor(name, number);
    static QSet<QString> logged;
    const QString asset = ensureNetworkLogoFile(dataDir, id);
    if (!logged.contains(name)) {
        logged.insert(name);
        if (id.isEmpty()) {
            std::fprintf(stderr, "zappe: ota: logo \"%s\" numero=%s sem marca, card de nome\n", qPrintable(name),
                         qPrintable(number));
        } else if (asset.isEmpty()) {
            std::fprintf(stderr, "zappe: ota: logo \"%s\" numero=%s marca=%s arquivo=nenhum (logos, ota-logos)\n",
                         qPrintable(name), qPrintable(number), qPrintable(id));
        } else {
            std::fprintf(stderr, "zappe: ota: logo \"%s\" numero=%s marca=%s arquivo=%s\n", qPrintable(name),
                         qPrintable(number), qPrintable(id), qPrintable(asset));
        }
    }
    const NetworkMark *mark = networkMark(id);
    if (!mark)
        return makeChannelCard(cacheDir, name);
    QImage probe;
    const bool readable = asset.isEmpty() || probe.load(asset);
    if (!asset.isEmpty() && !readable) {
        std::fprintf(stderr, "zappe: ota: logo \"%s\" arquivo ilegível %s\n", qPrintable(name), qPrintable(asset));
    }
    ArtPaths logo = paintNetworkMark(cacheDir, *mark, readable ? asset : QString());
    if (logo.artwork.isEmpty())
        return makeChannelCard(cacheDir, name);
    logo.mark = true;
    return logo;
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

QString makeNoiseSprite(const QString &cacheDir)
{
    const QString path = artFile(cacheDir, QStringLiteral("v%1-noise").arg(kArtVersion));
    if (QFileInfo::exists(path))
        return path;
    constexpr int n = 96;
    QImage image(n, n, QImage::Format_ARGB32);
    for (int y = 0; y < n; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < n; ++x) {
            quint32 h = 2166136261u;
            auto mix = [&h](quint32 v) {
                h ^= v;
                h *= 16777619u;
            };
            mix(static_cast<quint32>(x));
            mix(static_cast<quint32>(y));
            mix(static_cast<quint32>(x * 13 + y * 17));
            const int v = 28 + static_cast<int>(h % 200);
            line[x] = qRgba(v, v, v, 255);
        }
    }
    image.save(path, "PNG");
    return path;
}

} // namespace zappe
