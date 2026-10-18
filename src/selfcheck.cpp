#include "selfcheck.h"

#include "branding.h"
#include "catalog.h"
#include "guide.h"
#include "logiccheck.h"
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

    const QString harvestPath = dir.filePath(QStringLiteral("harvest.json"));
    QFile harvest(harvestPath);
    if (!harvest.open(QIODevice::WriteOnly))
        return fail("harvest catalog");
    harvest.write(QByteArray(R"json({
      "shelves": [
        {
          "id": "continue",
          "title": "Continue watching",
          "status": "error",
          "message": "no Chrome/Chromium application on the AT-SPI bus (is the nest running?)",
          "rows": []
        },
        {
          "id": "library",
          "title": "Library",
          "status": "ok",
          "rows": [
            {"title": "Northwind", "service": "library"},
            {"title": "no Chrome/Chromium application on the AT-SPI bus", "service": "netflix"}
          ]
        }
      ]
    })json"));
    harvest.close();
    const CatalogFile cleaned = loadCatalogFile(harvestPath);
    if (!cleaned.ok || cleaned.shelves.size() != 1 || cleaned.shelves.at(0).id != QLatin1String("library")
        || cleaned.shelves.at(0).rows.size() != 1
        || cleaned.shelves.at(0).rows.at(0).title != QLatin1String("Northwind"))
        return fail("harvest error shelf must disappear");
    QFile harvestAgain(harvestPath);
    if (!harvestAgain.open(QIODevice::ReadOnly) || harvestAgain.readAll().contains("AT-SPI"))
        return fail("harvest catalog was not rewritten");

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
    guide.setMirrorServiceIds({QStringLiteral("netflix")});
    guide.openMirror(QStringLiteral("netflix"));
    for (int i = 0; i < guide.shelfCount(); ++i) {
        if (guide.shelfShown(i))
            return fail("local catalog in streaming view");
    }
    guide.closeMirror();
    guide.back();
    guide.openMirror(QStringLiteral("netflix"));
    ShelfData blank;
    blank.id = QStringLiteral("mirror:netflix:0");
    blank.skillId = QStringLiteral("mirror:netflix");
    blank.title = QStringLiteral("Vazio");
    blank.status = QStringLiteral("ok");
    RowData emptyCard;
    emptyCard.title = QString();
    emptyCard.href = QString();
    blank.rows.append(emptyCard);
    guide.replaceServiceShelves(QStringLiteral("netflix"), {blank});
    guide.back();
    if (!guide.mirrorService().isEmpty() || guide.page() != QLatin1String("home")
        || guide.focusTitle().isEmpty() || guide.focusTitle() == QLatin1String("Vazio"))
        return fail("back leaves empty streaming view");
    guide.openMirror(QStringLiteral("netflix"));
    ShelfData none;
    none.id = QStringLiteral("mirror:netflix:empty");
    none.skillId = QStringLiteral("mirror:netflix");
    none.title = QStringLiteral("Netflix");
    none.status = QStringLiteral("empty");
    none.message = QStringLiteral("Nada nesta página ainda.");
    guide.replaceServiceShelves(QStringLiteral("netflix"), {none});
    guide.back();
    if (!guide.mirrorService().isEmpty() || guide.page() != QLatin1String("home"))
        return fail("back leaves shelf without cards");
    guide.openMirror(QStringLiteral("youtube"));
    ShelfData profiles;
    profiles.id = QStringLiteral("mirror:youtube:profiles");
    profiles.skillId = QStringLiteral("mirror:youtube");
    profiles.title = QStringLiteral("Perfis");
    profiles.status = QStringLiteral("ok");
    RowData account;
    account.title = QStringLiteral("Account menu");
    account.href = QStringLiteral("zappe://profile/Account%20menu");
    account.serviceLabel = QStringLiteral("YouTube");
    profiles.rows.append(account);
    guide.replaceServiceShelves(QStringLiteral("youtube"), {profiles});
    guide.back();
    if (!guide.mirrorService().isEmpty() || guide.page() != QLatin1String("home")
        || guide.focusTitle() == QLatin1String("Account menu"))
        return fail("back leaves account menu");
    const QStringList services = {QStringLiteral("netflix"), QStringLiteral("prime"),  QStringLiteral("apple"),
                                  QStringLiteral("disney"),  QStringLiteral("hbo"),    QStringLiteral("globoplay"),
                                  QStringLiteral("youtube")};
    for (const QString &id : services) {
        guide.openMirror(id);
        guide.toggleMenu();
        if (!guide.menuOpen() || guide.page() != QLatin1String("streaming") || guide.menuIndex() != 2)
            return fail("service menu");
        guide.move(1, 0);
        if (guide.page() != QLatin1String("settings") || guide.menuIndex() != 3)
            return fail("menu reaches settings");
        const int volume = guide.volume();
        guide.back();
        if (guide.menuOpen() || guide.page() != QLatin1String("home") || guide.menuIndex() != 0
            || !guide.mirrorService().isEmpty() || guide.focusTitle().trimmed().isEmpty()
            || guide.focusHref().trimmed().isEmpty() || !guide.shelfShown(guide.shelf())) {
            const QByteArray msg = QStringLiteral("home from %1").arg(id).toUtf8();
            return fail(msg.constData());
        }
        guide.move(1, 0);
        guide.move(0, 1);
        guide.move(0, -1);
        if (guide.menuOpen() || guide.page() != QLatin1String("home") || guide.menuIndex() != 0
            || guide.volume() != volume || guide.focusTitle().trimmed().isEmpty()) {
            const QByteArray msg = QStringLiteral("grid keeps keys from %1").arg(id).toUtf8();
            return fail(msg.constData());
        }
        guide.openMirror(id);
        guide.back();
        if (guide.menuOpen() || guide.page() != QLatin1String("home") || guide.menuIndex() != 0
            || guide.focusHref().trimmed().isEmpty() || !guide.shelfShown(guide.shelf())) {
            const QByteArray msg = QStringLiteral("back from %1").arg(id).toUtf8();
            return fail(msg.constData());
        }
        guide.openMirror(id);
        if (id == QLatin1String("youtube")) {
            ShelfData alta;
            alta.id = QStringLiteral("mirror:youtube:1");
            alta.skillId = QStringLiteral("mirror:youtube");
            alta.title = QStringLiteral("Em alta");
            alta.status = QStringLiteral("ok");
            RowData clip;
            clip.title = QStringLiteral("Clip do dia");
            clip.href = QStringLiteral("https://www.youtube.com/watch?v=abcdefghijk");
            clip.service = QStringLiteral("youtube");
            alta.rows.append(clip);
            guide.replaceServiceShelves(QStringLiteral("youtube"), {alta});
            int altaShelf = -1;
            for (int i = 0; i < guide.shelfCount(); ++i) {
                const ShelfData *shelf = guide.shelves()->shelfAt(i);
                if (shelf && shelf->title == QLatin1String("Em alta") && guide.shelfShown(i))
                    altaShelf = i;
            }
            if (altaShelf < 0)
                return fail("youtube em alta shelf");
            guide.setInitial(altaShelf, 0, false);
            if (guide.focusTitle() != QLatin1String("Clip do dia") || guide.page() != QLatin1String("streaming"))
                return fail("focus on youtube em alta");
            guide.toggleMenu();
            guide.move(1, 0);
            if (!guide.menuOpen() || guide.page() != QLatin1String("settings"))
                return fail("home from em alta opened settings");
        }
        guide.home();
        if (guide.menuOpen() || guide.page() != QLatin1String("home") || guide.menuIndex() != 0
            || !guide.mirrorService().isEmpty() || guide.focusTitle().trimmed().isEmpty()
            || guide.focusHref().trimmed().isEmpty() || !guide.shelfShown(guide.shelf())
            || guide.focusTitle() == QLatin1String("Clip do dia")) {
            const QByteArray msg = QStringLiteral("home key from %1").arg(id).toUtf8();
            return fail(msg.constData());
        }
    }
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
    return runLogicSelfCheck();
}

} // namespace zappe
