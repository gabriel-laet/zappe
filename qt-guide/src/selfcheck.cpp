#include "selfcheck.h"

#include "branding.h"
#include "catalog.h"
#include "guide.h"
#include "posters.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace zappe {
namespace {

int fail(const char *message)
{
    std::fprintf(stderr, "self-check: %s\n", message);
    return 1;
}

bool cornerIsClear(const QString &path)
{
    const QImage image(path);
    if (image.isNull() || image.width() < 8)
        return false;
    return qAlpha(image.pixel(0, 0)) < 20 && qAlpha(image.pixel(image.width() / 2, image.height() / 2)) > 200;
}

} // namespace

int runSelfCheck()
{
    QFile sample(QStringLiteral(":/zappe/data/catalog.json"));
    if (!sample.open(QIODevice::ReadOnly))
        return fail("bundled catalog missing");
    const QByteArray bytes = sample.readAll();
    sample.close();

    QTemporaryDir dir;
    if (!dir.isValid())
        return fail("temp dir");
    const QString samplePath = dir.filePath(QStringLiteral("catalog.json"));
    QFile out(samplePath);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size())
        return fail("write sample");
    out.close();

    const CatalogFile catalog = loadCatalogFile(samplePath);
    if (!catalog.ok)
        return fail("sample catalog did not parse");
    if (catalog.shelves.size() != 4)
        return fail("expected 4 shelves");
    if (catalog.shelves.at(0).id != QLatin1String("live") || catalog.shelves.at(0).title != QLatin1String("Ao vivo")
        || !catalog.shelves.at(0).live)
        return fail("live shelf");
    if (catalog.shelves.at(0).rows.size() < 2 || !catalog.shelves.at(0).rows.at(0).live
        || !catalog.shelves.at(0).rows.at(0).wide || catalog.shelves.at(0).rows.at(0).title != QLatin1String("North Desk"))
        return fail("live row");
    if (catalog.shelves.at(1).id != QLatin1String("continue") || catalog.shelves.at(1).status != QLatin1String("ok"))
        return fail("continue shelf");
    if (catalog.shelves.at(1).rows.size() < 2 || catalog.shelves.at(1).rows.at(0).title != QLatin1String("Northwind")
        || catalog.shelves.at(1).rows.at(0).live)
        return fail("first continue row");
    if (!catalog.shelves.at(1).rows.at(0).wide || catalog.shelves.at(2).rows.at(0).wide)
        return fail("wide vs poster");
    if (catalog.shelves.at(3).title != QLatin1String("Set aside"))
        return fail("later shelf title");

    const QString badPath = dir.filePath(QStringLiteral("bad.json"));
    QFile bad(badPath);
    bad.open(QIODevice::WriteOnly);
    bad.write("{");
    bad.close();
    if (loadCatalogFile(badPath).ok)
        return fail("invalid json should not be ok");

    const Branding defaults = loadBranding(dir.filePath(QStringLiteral("missing-brand")));
    if (defaults.name != QLatin1String("Zappe") || defaults.accent != QLatin1String("#E85A1B")
        || defaults.background != QLatin1String("#000000") || defaults.source != QLatin1String("default"))
        return fail("default branding");

    QImage solid(32, 32, QImage::Format_ARGB32);
    solid.fill(Qt::red);
    const QString logo = dir.filePath(QStringLiteral("logo.png"));
    if (!solid.save(logo))
        return fail("save logo");
    const QString brandJson = dir.filePath(QStringLiteral("branding.json"));
    QFile brandFile(brandJson);
    brandFile.open(QIODevice::WriteOnly);
    brandFile.write(QByteArrayLiteral(R"({
      "name": "My TV",
      "accent": "not a color",
      "logo": "logo.png",
      "splash": "logo.png",
      "tagline": "What are we watching?",
      "idle": {"mode": "screensaver", "asset": "logo.png", "timeoutSeconds": 120, "animation": "soft-breathe"},
      "theme": {"style": "apple-tv", "background": "#101014", "focusRing": "subtle-scale", "accent": "#3366CC"}
    })"));
    brandFile.close();
    const Branding user = loadBranding(dir.path());
    if (user.name != QLatin1String("My TV") || user.source != QLatin1String("user"))
        return fail("user name");
    if (user.accent != QLatin1String("#3366CC"))
        return fail("theme accent should win and the bad top-level accent should be ignored");
    if (user.background != QLatin1String("#101014") || user.themeFocus != QLatin1String("subtle-scale"))
        return fail("theme fields");
    if (user.tagline != QLatin1String("What are we watching?"))
        return fail("tagline");
    if (user.logoPath.isEmpty() || user.splashPath.isEmpty() || user.idleAssetPath.isEmpty())
        return fail("assets");
    if (user.idleTimeoutSeconds != 120 || user.idleMode != QLatin1String("screensaver"))
        return fail("idle");
    if (std::abs(user.focusScale - 1.08) > 0.001)
        return fail("subtle scale");

    const QString artDir = dir.filePath(QStringLiteral("art"));
    const QString mark = makeCircleMark(artDir, logo, 64);
    if (!cornerIsClear(mark))
        return fail("circle mark");

    const ArtPaths poster = makePoster(artDir, QStringLiteral("Northwind"), QStringLiteral("Library"), true);
    const QImage art(poster.artwork);
    const QImage ambient(poster.ambient);
    if (art.isNull() || art.width() != 960 || ambient.isNull())
        return fail("poster");
    int minR = 255;
    int maxR = 0;
    for (int y = 0; y < art.height(); y += 20) {
        for (int x = 0; x < art.width(); x += 20) {
            const int r = qRed(art.pixel(x, y));
            minR = std::min(minR, r);
            maxR = std::max(maxR, r);
        }
    }
    if (maxR - minR < 12)
        return fail("poster looks flat");
    if (!poster.color.startsWith(QLatin1Char('#')))
        return fail("poster color");

    const QString shadow = makeShadowSprite(artDir);
    const QImage shadowImage(shadow);
    if (shadowImage.isNull() || qAlpha(shadowImage.pixel(0, 0)) > 30)
        return fail("shadow sprite");

    const QImage noise(makeNoiseSprite(artDir));
    if (noise.isNull() || noise.width() < 32)
        return fail("noise sprite");

    Guide guide;
    guide.setBranding(defaults);
    guide.setCatalog(catalog);
    if (guide.focusTitle() != QLatin1String("North Desk") || !guide.focusLive() || guide.shelf() != 0)
        return fail("live focus");
    guide.setPreview(false, QStringLiteral("streaming"), false, {});
    if (guide.page() != QLatin1String("streaming") || guide.shelfShown(0) || !guide.shelfShown(1) || guide.shelf() != 1)
        return fail("streaming page");
    guide.move(-1, 0);
    if (guide.shelf() != 1)
        return fail("streaming skips live");
    guide.move(1, 0);
    if (guide.shelf() != 2)
        return fail("streaming next shelf");
    guide.setPreview(true, QStringLiteral("live"), false, {});
    if (!guide.menuOpen() || guide.page() != QLatin1String("live") || guide.menuIndex() != 1 || !guide.shelfShown(0)
        || guide.shelfShown(1))
        return fail("live menu");
    guide.back();
    if (guide.menuOpen() || guide.page() != QLatin1String("live"))
        return fail("menu back");
    guide.back();
    if (guide.page() != QLatin1String("home") || !guide.shelfShown(1))
        return fail("back home");
    guide.adjustVolume(5);
    if (guide.volume() != 45 || !guide.volumeHud())
        return fail("volume");
    guide.toggleMute();
    if (!guide.muted())
        return fail("mute");
    if (guide.episodeTitle(3) != QString::fromUtf8("Turno da noite"))
        return fail("episode title");

    std::printf("self-check: ok (%d shelves, brand Zappe, poster %dx%d)\n", int(catalog.shelves.size()), art.width(),
                art.height());
    return 0;
}

} // namespace zappe
