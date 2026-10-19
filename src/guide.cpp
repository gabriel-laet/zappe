#include "guide.h"

#include "branding.h"
#include "display.h"
#include "mirror.h"
#include "ota.h"
#include "posters.h"
#include "util.h"

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QUrl>
#include <QFontDatabase>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace zappe {

bool restoreGuideFocus(const QVector<ShelfData> &shelves, const QVector<int> &shown, const QString &keepHref,
                       const QString &keepTitle, int keepShelf, int keepIndex, int &shelf, int &index)
{
    auto shownOk = [&](int row) { return row >= 0 && row < shown.size() && shown.at(row) != 0 && row < shelves.size(); };
    if (!keepHref.isEmpty()) {
        for (int s = 0; s < shelves.size(); ++s) {
            if (!shownOk(s))
                continue;
            for (int i = 0; i < shelves.at(s).rows.size(); ++i) {
                if (shelves.at(s).rows.at(i).href == keepHref) {
                    shelf = s;
                    index = i;
                    return true;
                }
            }
        }
    }
    if (!keepTitle.isEmpty()) {
        const int prefer = shownOk(keepShelf) ? keepShelf : -1;
        for (int pass = 0; pass < 2; ++pass) {
            for (int s = 0; s < shelves.size(); ++s) {
                if (!shownOk(s))
                    continue;
                if (pass == 0 && s != prefer)
                    continue;
                if (pass == 1 && s == prefer)
                    continue;
                for (int i = 0; i < shelves.at(s).rows.size(); ++i) {
                    if (shelves.at(s).rows.at(i).title == keepTitle) {
                        shelf = s;
                        index = i;
                        return true;
                    }
                }
            }
        }
    }
    if (shownOk(keepShelf)) {
        shelf = keepShelf;
        index = keepIndex;
        return true;
    }
    int pick = -1;
    const int start = std::max(0, keepShelf);
    for (int s = start; s < shelves.size(); ++s) {
        if (shownOk(s)) {
            pick = s;
            break;
        }
    }
    if (pick < 0) {
        for (int s = 0; s < shelves.size(); ++s) {
            if (shownOk(s)) {
                pick = s;
                break;
            }
        }
    }
    if (pick < 0)
        return false;
    shelf = pick;
    index = keepIndex;
    return true;
}

QString cacheDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString dir = QDir(base).filePath(QStringLiteral("art"));
    QDir().mkpath(dir);
    return dir;
}

QString pickFont(const QStringList &prefs)
{
    const QStringList families = QFontDatabase::families();
    for (const QString &pref : prefs) {
        if (families.contains(pref))
            return pref;
    }
    return prefs.isEmpty() ? QString() : prefs.last();
}

Guide::Guide(QObject *parent)
    : QObject(parent)
    , m_shelves(new ShelfModel(this))
{
    m_uiFont = pickFont({QStringLiteral("Inter"), QStringLiteral("Noto Sans"), QStringLiteral("Noto Sans Display"),
                         QStringLiteral("sans-serif")});
    m_monoFont = pickFont({QStringLiteral("JetBrains Mono"), QStringLiteral("Noto Sans Mono"),
                           QStringLiteral("DejaVu Sans Mono"), QStringLiteral("monospace")});
    m_plain.name = QStringLiteral("Zappe");
    m_plain.accent = QStringLiteral("#E85A1B");
    m_plain.background = QStringLiteral("#000000");
    m_plain.focusScale = 1.08;
    m_plain.source = QStringLiteral("default");
    m_plain.accentInk = inkFor(QColor(m_plain.accent));
}

void Guide::applyBrand(const Branding &branding)
{
    const bool accentChanged = m_glowUrl.isEmpty() || m_brand.accent != branding.accent;
    m_brand = branding;
    const QString dir = cacheDir();
    QColor accent = QColor::fromString(m_brand.accent);
    if (!accent.isValid())
        accent = QColor(QStringLiteral("#E85A1B"));
    if (m_brand.shadowPath.isEmpty())
        m_brand.shadowPath = makeShadowSprite(dir);
    if (accentChanged || m_brand.glowPath.isEmpty())
        m_brand.glowPath = makeGlowSprite(dir, accent);
    if (!m_brand.logoPath.isEmpty())
        m_brand.logoMarkPath = makeCircleMark(dir, m_brand.logoPath, 256);
    else
        m_brand.logoMarkPath.clear();
    m_logoUrl = fileUrl(m_brand.logoMarkPath);
    const QString splash = !m_brand.splashPath.isEmpty() ? m_brand.splashPath : m_brand.logoMarkPath;
    m_splashUrl = fileUrl(splash);
    m_shadowUrl = fileUrl(m_brand.shadowPath);
    m_glowUrl = fileUrl(m_brand.glowPath);
}

void Guide::setBranding(const Branding &branding)
{
    m_loaded = branding;
    m_noiseUrl = fileUrl(makeNoiseSprite(cacheDir()));
    applyBrand(m_brandingOn ? m_loaded : m_plain);
    if (m_focusTitle.isEmpty())
        syncFocusFields();
}

void Guide::setCatalog(CatalogFile catalog)
{
    m_catalogPath = catalog.path;
    m_baseShelves = std::move(catalog.shelves);
    m_mirrorShelves.clear();
    m_mirrorService.clear();
    publishShelves(true);
}

void Guide::setInitial(int shelf, int index, bool detail)
{
    const int shelves = shelfCount();
    if (shelves == 0) {
        syncFocusFields();
        return;
    }
    m_shelf = std::clamp(shelf, 0, shelves - 1);
    m_index = index;
    clampIndex();
    m_detailOpen = detail && currentRow() != nullptr;
    if (m_detailOpen) {
        m_detailZone = 0;
        m_detailButton = 0;
        m_episode = 0;
    }
    syncFocusFields();
}

void Guide::setApplianceDir(const QString &dir)
{
    m_dataDir = dir;
    const QString resolution = loadResolution(dir);
    if (resolution != m_resolution) {
        m_resolution = resolution;
        emit settingsChanged();
    }
}

void Guide::setCoalesceInput(bool on)
{
    m_coalesce = on;
}

void Guide::toast(const QString &text)
{
    showToast(text);
}

bool Guide::coalesce(const QString &key)
{
    if (!m_coalesce)
        return true;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 prev = m_coalesceAt.value(key);
    if (now - prev < 25)
        return false;
    m_coalesceAt.insert(key, now);
    return true;
}

void Guide::setChrome(bool windowed, bool frameless, bool skipSplash, bool showFps)
{
    m_windowed = windowed;
    m_frameless = frameless;
    m_skipSplash = skipSplash;
    m_showFps = showFps;
}

void Guide::setPreview(bool openMenu, const QString &page, bool pinVolume, const QString &toast)
{
    if (page == QLatin1String("home") || page == QLatin1String("live") || page == QLatin1String("streaming")
        || page == QLatin1String("settings")) {
        if (page == QLatin1String("settings") && m_detailOpen) {
            m_detailOpen = false;
            emit detailChanged();
        }
        setPage(page);
    }
    if (openMenu) {
        if (m_detailOpen) {
            m_detailOpen = false;
            emit detailChanged();
        }
        m_menuIndex = menuForPage(m_page);
        m_menuOpen = true;
        emit menuChanged();
    }
    if (pinVolume) {
        m_pinVolume = true;
        m_volume = 64;
        m_muted = false;
        m_volumeHud = true;
        emit volumeChanged();
        emit hudChanged();
    }
    if (!toast.isEmpty()) {
        m_toastText = toast;
        m_toastOn = true;
        m_pinToast = true;
        emit toastChanged();
    }
}

void Guide::attachWindow(QQuickWindow *window)
{
    if (!window || !m_showFps)
        return;
    auto mark = std::chrono::steady_clock::now();
    int frames = 0;
    connect(window, &QQuickWindow::frameSwapped, this, [this, mark, frames]() mutable {
        ++frames;
        const auto now = std::chrono::steady_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - mark).count();
        if (ms < 500)
            return;
        m_fps = static_cast<int>(std::lround(frames * 1000.0 / double(ms)));
        frames = 0;
        mark = now;
        emit fpsChanged();
    });
}

int Guide::shelfCount() const
{
    return m_shelves->rowCount();
}

bool Guide::shelfShown(int index) const
{
    const ShelfData *shelf = m_shelves->shelfAt(index);
    if (!shelf || localMirrorStandIn(shelf))
        return false;
    if (m_page == QLatin1String("settings"))
        return false;
    if (m_page == QLatin1String("live"))
        return shelf->live;
    if (m_page == QLatin1String("streaming")) {
        if (!m_mirrorService.isEmpty())
            return shelf->session && shelf->skillId == QStringLiteral("mirror:") + m_mirrorService;
        return !shelf->live;
    }
    if (shelf->skillId.startsWith(QStringLiteral("mirror:")))
        return shelf->id.contains(QStringLiteral(":continue")) || shelf->id.endsWith(QStringLiteral(":profiles"));
    return true;
}

QString Guide::episodeTitle(int index) const
{
    static const char *titles[] = {
        "Abertura",
        "O sinal",
        "Janela acesa",
        "Turno da noite",
        "Mapa dobrado",
        "Última chamada",
    };
    if (index < 0 || index >= episodeCount())
        return {};
    return QString::fromUtf8(titles[index]);
}

int Guide::menuForPage(const QString &page)
{
    if (page == QLatin1String("live"))
        return 1;
    if (page == QLatin1String("streaming"))
        return 2;
    if (page == QLatin1String("settings"))
        return 3;
    return 0;
}

QString Guide::pageForMenu(int index)
{
    switch (index) {
    case 1:
        return QStringLiteral("live");
    case 2:
        return QStringLiteral("streaming");
    case 3:
        return QStringLiteral("settings");
    default:
        return QStringLiteral("home");
    }
}

void Guide::setPage(const QString &page)
{
    if (page != QLatin1String("settings"))
        closeTvTest();
    if (page == m_page)
        return;
    m_page = page;
    m_menuIndex = menuForPage(page);
    ++m_layoutEpoch;
    emit layoutChanged();
    clampFocusToPage();
    emit pageChanged();
    emit menuChanged();
    emit focusChanged();
}

void Guide::setMenuIndex(int index)
{
    index = std::clamp(index, 0, 3);
    const QString page = pageForMenu(index);
    if (index == m_menuIndex && page == m_page)
        return;
    m_menuIndex = index;
    if (page != m_page) {
        if (page != QLatin1String("settings"))
            closeTvTest();
        m_page = page;
        clampFocusToPage();
        emit pageChanged();
        emit focusChanged();
    }
    emit menuChanged();
}

void Guide::clampIndex()
{
    const int rows = m_shelves->rowsAt(m_shelf) ? m_shelves->rowsAt(m_shelf)->rowCount() : 0;
    m_index = rows == 0 ? 0 : std::clamp(m_index, 0, rows - 1);
}

void Guide::clampFocusToPage()
{
    if (shelfCount() == 0 || m_page == QLatin1String("settings"))
        return;
    if (!shelfShown(m_shelf)) {
        int pick = m_shelf;
        for (int i = 0; i < shelfCount(); ++i) {
            if (shelfShown(i)) {
                pick = i;
                break;
            }
        }
        m_shelf = pick;
    }
    clampIndex();
    syncFocusFields();
}

void Guide::move(int dShelf, int dIndex)
{
    if (!coalesce(QStringLiteral("move:%1:%2").arg(dShelf).arg(dIndex)))
        return;
    if (m_menuOpen) {
        if (dShelf != 0)
            setMenuIndex(m_menuIndex + dShelf);
        if (dIndex != 0) {
            m_menuOpen = false;
            emit menuChanged();
        }
        return;
    }
    if (m_detailOpen) {
        moveDetail(dShelf, dIndex);
        return;
    }
    if (m_tvTest) {
        const int step = dShelf != 0 ? dShelf : dIndex;
        if (step != 0) {
            const int next = std::clamp(m_tvTestIndex + (step > 0 ? 1 : -1), 0, 4);
            if (next != m_tvTestIndex) {
                m_tvTestIndex = next;
                emit tvTestChanged();
            }
        }
        return;
    }
    if (m_page == QLatin1String("settings")) {
        moveSettings(dShelf, dIndex);
        return;
    }
    if (shelfCount() == 0)
        return;
    moveShelves(dShelf, dIndex);
}

void Guide::moveShelves(int dShelf, int dIndex)
{
    bool changed = false;
    if (dShelf != 0) {
        const int step = dShelf > 0 ? 1 : -1;
        int next = m_shelf;
        for (int i = m_shelf + step; i >= 0 && i < shelfCount(); i += step) {
            if (shelfShown(i)) {
                next = i;
                break;
            }
        }
        if (next != m_shelf) {
            m_shelf = next;
            clampIndex();
            changed = true;
        }
    }
    if (dIndex != 0) {
        const int rows = m_shelves->rowsAt(m_shelf) ? m_shelves->rowsAt(m_shelf)->rowCount() : 0;
        if (rows > 0) {
            const int next = std::clamp(m_index + dIndex, 0, rows - 1);
            if (next != m_index) {
                m_index = next;
                changed = true;
            }
        }
    }
    if (changed)
        publishFocus();
}

void Guide::moveDetail(int dShelf, int dIndex)
{
    bool changed = false;
    if (dShelf > 0 && m_detailZone == 0 && !m_focusMirrored) {
        m_detailZone = 1;
        changed = true;
    } else if (dShelf < 0 && m_detailZone == 1) {
        m_detailZone = 0;
        changed = true;
    }
    if (dIndex != 0 && m_detailZone == 0) {
        int next = m_detailButton + dIndex;
        if (m_focusMirrored && next == 1)
            next += dIndex > 0 ? 1 : -1;
        next = std::clamp(next, 0, 2);
        if (next != m_detailButton) {
            m_detailButton = next;
            changed = true;
        }
    } else if (dIndex != 0 && m_detailZone == 1 && !m_focusMirrored) {
        const int next = std::clamp(m_episode + dIndex, 0, episodeCount() - 1);
        if (next != m_episode) {
            m_episode = next;
            changed = true;
        }
    }
    if (changed)
        emit detailChanged();
}

void Guide::moveSettings(int dShelf, int dIndex)
{
    // 0 resolução, 1 marca, 2 estado, 3 força, 4 volume, 5 mudo, 6 HDMI, 7 Testar TV.
    constexpr int kLast = 7;
    if (dShelf != 0) {
        const int next = std::clamp(m_settingsIndex + dShelf, 0, kLast);
        if (next != m_settingsIndex) {
            m_settingsIndex = next;
            emit settingsChanged();
        }
    }
    if (dIndex != 0) {
        if (m_settingsIndex == 0)
            nudgeResolution(dIndex);
        else if (m_settingsIndex == 1)
            setBrandingEnabled(dIndex > 0);
        else if (m_settingsIndex != 7)
            emit settingsCommand(m_settingsIndex, dIndex > 0 ? 1 : -1);
    }
}

void Guide::nudgeResolution(int direction)
{
    const QString next = direction > 0 ? QStringLiteral("4K") : QStringLiteral("1080p");
    if (next == m_resolution)
        return;
    m_resolution = next;
    if (!m_dataDir.isEmpty())
        saveResolution(m_dataDir, m_resolution);
    emit settingsChanged();
    showToast(m_resolution == QLatin1String("4K") ? QStringLiteral("4K no próximo início do kiosk")
                                                  : QStringLiteral("1080p no próximo início do kiosk"));
}

void Guide::setBrandingEnabled(bool on)
{
    if (m_brandingOn == on)
        return;
    m_brandingOn = on;
    applyBrand(on ? m_loaded : m_plain);
    emit brandingChanged();
    if (on && brandCustom())
        showToast(m_loaded.name);
    else if (on)
        showToast(QStringLiteral("Sem arquivo de marca — Zappe"));
    else
        showToast(QStringLiteral("Marca padrão"));
}

void Guide::activate()
{
    if (!coalesce(QStringLiteral("activate")))
        return;
    if (m_menuOpen) {
        m_menuOpen = false;
        emit menuChanged();
        return;
    }
    if (m_tvTest) {
        emit tvTestCommand(m_tvTestIndex);
        return;
    }
    if (m_page == QLatin1String("settings") && !m_detailOpen) {
        if (m_settingsIndex == 0)
            nudgeResolution(m_resolution == QLatin1String("1080p") ? 1 : -1);
        else if (m_settingsIndex == 1)
            setBrandingEnabled(!m_brandingOn);
        else if (m_settingsIndex == 7)
            openTvTest();
        else
            emit settingsCommand(m_settingsIndex, 0);
        return;
    }
    if (m_detailOpen) {
        activateDetail();
        return;
    }
    const ShelfData *shelf = m_shelves->shelfAt(m_shelf);
    QString retryService;
    if (shelf && mirrorShelfRetries(shelf->status, shelf->skillId, retryService)) {
        emit mirrorRetryRequested(retryService);
        return;
    }
    if (!currentRow())
        return;
    if (shelf && shelf->id == QLatin1String("apps")) {
        emit serviceRequested(currentRow()->service);
        return;
    }
    if (currentRow()->href.startsWith(QStringLiteral("zappe://profile/"))) {
        const QString encoded = currentRow()->href.mid(QStringLiteral("zappe://profile/").size());
        emit profileRequested(currentRow()->service, QUrl::fromPercentEncoding(encoded.toUtf8()));
        return;
    }
    if (!m_focusLive) {
        const QString service = mirrorRowService(currentRow()->service, currentRow()->href);
        if (!service.isEmpty()) {
            emit serviceRequested(service);
            return;
        }
    }
    if (m_focusLive) {
        pulseCard();
        emit playRequested(m_focusTitle, m_focusService, m_focusHref);
        return;
    }
    m_detailOpen = true;
    m_detailZone = 0;
    m_detailButton = 0;
    m_episode = 0;
    refreshSaved();
    emit detailChanged();
    emit focusChanged();
}

void Guide::pulseCard()
{
    m_cardDown = true;
    emit cardDownChanged();
    QTimer::singleShot(160, this, [this] {
        m_cardDown = false;
        emit cardDownChanged();
    });
}

void Guide::activateDetail()
{
    if (m_detailZone == 0 && m_detailButton == 0) {
        pulseCard();
        emit playRequested(m_focusTitle, m_focusService, m_focusHref);
        return;
    }
    if (m_detailZone == 0 && m_detailButton == 1) {
        if (!m_focusMirrored)
            showToast(QStringLiteral("Trailer · ") + m_focusTitle);
    } else if (m_detailZone == 0 && m_detailButton == 2)
        toggleSaved();
    else if (!m_focusMirrored)
        showToast(episodeTitle(m_episode));
    emit activated();
}

void Guide::toggleSaved()
{
    const QString key = focusKey();
    if (key.isEmpty())
        return;
    if (m_savedKeys.contains(key)) {
        m_savedKeys.remove(key);
        m_saved = false;
        showToast(QStringLiteral("Removido da lista"));
    } else {
        m_savedKeys.insert(key);
        m_saved = true;
        showToast(QStringLiteral("Na sua lista"));
    }
    emit savedChanged();
}

void Guide::ensureFocusableShelf()
{
    auto good = [this](int shelf) {
        if (!shelfShown(shelf))
            return false;
        const ShelfData *data = m_shelves->shelfAt(shelf);
        if (!data || data->id.endsWith(QStringLiteral(":profiles")))
            return false;
        for (const RowData &row : data->rows) {
            if (!row.title.trimmed().isEmpty() && !row.href.trimmed().isEmpty())
                return true;
        }
        return false;
    };
    if (good(m_shelf))
        return;
    for (int i = 0; i < shelfCount(); ++i) {
        if (!good(i))
            continue;
        m_shelf = i;
        m_index = 0;
        clampIndex();
        syncFocusFields();
        emit focusChanged();
        return;
    }
}

void Guide::home()
{
    if (!coalesce(QStringLiteral("home")))
        return;
    closeTvTest();
    const bool detailWas = m_detailOpen;
    const bool menuWas = m_menuOpen;
    m_detailOpen = false;
    m_menuOpen = false;
    m_menuIndex = 0;
    m_mirrorService.clear();
    if (m_page != QLatin1String("home"))
        setPage(QStringLiteral("home"));
    int shelf = 0;
    for (int i = 0; i < shelfCount(); ++i) {
        if (shelfShown(i)) {
            shelf = i;
            break;
        }
    }
    m_shelf = shelf;
    m_index = 0;
    clampIndex();
    ++m_layoutEpoch;
    emit layoutChanged();
    publishFocus();
    if (detailWas)
        emit detailChanged();
    if (menuWas)
        emit menuChanged();
}

void Guide::back()
{
    if (!coalesce(QStringLiteral("back")))
        return;
    if (m_menuOpen) {
        m_menuOpen = false;
        emit menuChanged();
        return;
    }
    if (m_detailOpen) {
        m_detailOpen = false;
        emit detailChanged();
        emit focusChanged();
        return;
    }
    // Uma view de streaming, mesmo vazia ou só com card vazio, sempre sai.
    if (!m_mirrorService.isEmpty() || m_page == QLatin1String("streaming")) {
        m_mirrorService.clear();
        if (m_page != QLatin1String("home"))
            setPage(QStringLiteral("home"));
        else {
            ++m_layoutEpoch;
            emit layoutChanged();
            clampFocusToPage();
            emit focusChanged();
        }
        ensureFocusableShelf();
        return;
    }
    if (m_tvTest) {
        closeTvTest();
        return;
    }
    if (m_page != QLatin1String("home")) {
        setPage(QStringLiteral("home"));
        ensureFocusableShelf();
        return;
    }
    ensureFocusableShelf();
}

void Guide::openTvTest()
{
    if (m_page != QLatin1String("settings"))
        setPage(QStringLiteral("settings"));
    if (m_tvTest)
        return;
    m_tvTest = true;
    m_tvTestIndex = 0;
    emit tvTestChanged();
}

void Guide::closeTvTest()
{
    if (!m_tvTest)
        return;
    m_tvTest = false;
    emit tvTestChanged();
}

void Guide::pushDial(const QString &glyph)
{
    const QString next = appendDialText(m_dialText, glyph);
    if (next == m_dialText)
        return;
    m_dialText = next;
    emit dialChanged();
}

void Guide::clearDial()
{
    if (m_dialText.isEmpty())
        return;
    m_dialText.clear();
    emit dialChanged();
}

void Guide::refreshLiveChannels()
{
    CatalogFile catalog;
    catalog.path = m_catalogPath;
    catalog.shelves = m_baseShelves;
    const QString data = m_dataDir.isEmpty() ? resolveDataDir() : m_dataDir;
    mergeLiveChannels(catalog, listOtaChannels());
    decorateCatalog(catalog, data, cacheDir());
    m_baseShelves = std::move(catalog.shelves);
    publishShelves(false);
}

void Guide::noteLiveEpg(const QString &href, const QString &line)
{
    if (href.isEmpty() || line.isEmpty())
        return;
    bool changed = false;
    for (ShelfData &shelf : m_baseShelves) {
        for (RowData &row : shelf.rows) {
            if (row.href != href)
                continue;
            row.synopsis = line;
            changed = true;
        }
    }
    if (changed)
        publishShelves(false);
}

void Guide::noteLiveFrame(const QString &href, const QString &artwork, const QString &ambient, const QString &color)
{
    if (href.isEmpty() || artwork.isEmpty())
        return;
    bool changed = false;
    for (ShelfData &shelf : m_baseShelves) {
        for (RowData &row : shelf.rows) {
            if (row.href != href)
                continue;
            row.artwork = artwork;
            row.ambient = ambient;
            row.color = color;
            row.frame = true;
            changed = true;
        }
    }
    if (changed)
        publishShelves(false);
}

void Guide::publishShelves(bool resetFocus)
{
    const QString keepHref = resetFocus ? QString() : m_focusHref;
    const QString keepTitle = resetFocus ? QString() : m_focusTitle;
    const int keepShelf = m_shelf;
    const int keepIndex = m_index;
    QVector<ShelfData> merged;
    merged.reserve(m_baseShelves.size() + m_mirrorShelves.size());
    bool inserted = false;
    for (const ShelfData &shelf : m_baseShelves) {
        merged.append(shelf);
        if (!inserted && shelf.id == QLatin1String("apps")) {
            merged += m_mirrorShelves;
            inserted = true;
        }
    }
    if (!inserted)
        merged += m_mirrorShelves;
    m_shelves->setShelves(std::move(merged));
    ++m_layoutEpoch;
    emit layoutChanged();
    if (resetFocus) {
        m_shelf = 0;
        m_index = 0;
        m_detailOpen = false;
    } else {
        QVector<ShelfData> shelves;
        QVector<int> shown;
        shelves.reserve(shelfCount());
        shown.reserve(shelfCount());
        for (int i = 0; i < shelfCount(); ++i) {
            const ShelfData *shelf = m_shelves->shelfAt(i);
            shelves.append(shelf ? *shelf : ShelfData{});
            shown.append(shelfShown(i) ? 1 : 0);
        }
        int shelf = keepShelf;
        int index = keepIndex;
        if (restoreGuideFocus(shelves, shown, keepHref, keepTitle, keepShelf, keepIndex, shelf, index)) {
            m_shelf = shelf;
            m_index = index;
        }
    }
    clampFocusToPage();
    syncFocusFields();
    emit focusChanged();
}

bool Guide::localMirrorStandIn(const ShelfData *shelf) const
{
    if (!shelf || shelf->id == QLatin1String("apps") || shelf->rows.isEmpty() || m_mirrorIds.isEmpty())
        return false;
    if (shelf->skillId.startsWith(QStringLiteral("mirror:")))
        return false;
    for (const RowData &row : shelf->rows) {
        if (!m_mirrorIds.contains(row.service.trimmed().toLower()))
            return false;
    }
    return true;
}

void Guide::setMirrorServiceIds(const QStringList &ids)
{
    m_mirrorIds.clear();
    for (const QString &id : ids)
        m_mirrorIds.insert(id.trimmed().toLower());
    ++m_layoutEpoch;
    emit layoutChanged();
    clampFocusToPage();
    emit focusChanged();
}

void Guide::openMirror(const QString &serviceId)
{
    if (serviceId.isEmpty())
        return;
    m_mirrorService = serviceId;
    m_menuOpen = false;
    m_detailOpen = false;
    m_page = QStringLiteral("streaming");
    m_menuIndex = menuForPage(m_page);
    ++m_layoutEpoch;
    emit layoutChanged();
    emit menuChanged();
    emit detailChanged();
    emit pageChanged();
    clampFocusToPage();
    emit focusChanged();
}

void Guide::closeMirror()
{
    if (m_mirrorService.isEmpty())
        return;
    m_mirrorService.clear();
    ++m_layoutEpoch;
    emit layoutChanged();
    clampFocusToPage();
    emit focusChanged();
}

void Guide::replaceServiceShelves(const QString &serviceId, QVector<ShelfData> shelves)
{
    const QString skill = QStringLiteral("mirror:") + serviceId;
    QVector<ShelfData> kept;
    for (const ShelfData &shelf : m_mirrorShelves) {
        if (shelf.skillId != skill)
            kept.append(shelf);
    }
    for (ShelfData &shelf : shelves) {
        shelf.skillId = skill;
        shelf.session = true;
        if (shelf.status.isEmpty())
            shelf.status = QStringLiteral("ok");
        kept.append(shelf);
    }
    m_mirrorShelves = std::move(kept);
    publishShelves(false);
}

void Guide::toggleMenu()
{
    if (!coalesce(QStringLiteral("menu")))
        return;
    if (m_menuOpen) {
        m_menuOpen = false;
        emit menuChanged();
        return;
    }
    if (m_detailOpen) {
        m_detailOpen = false;
        emit detailChanged();
    }
    m_menuIndex = menuForPage(m_page);
    m_menuOpen = true;
    emit menuChanged();
    emit focusChanged();
}

void Guide::adjustVolume(int delta)
{
    if (!coalesce(QStringLiteral("volume:%1").arg(delta > 0 ? 1 : -1)))
        return;
    if (delta > 0 && m_muted)
        m_muted = false;
    const int next = std::clamp(m_volume + delta, 0, 100);
    if (next == 0)
        m_muted = true;
    m_volume = next;
    revealVolume();
    emit volumeChanged();
}

void Guide::toggleMute()
{
    if (!coalesce(QStringLiteral("mute")))
        return;
    m_muted = !m_muted;
    revealVolume();
    emit volumeChanged();
}

void Guide::revealVolume()
{
    m_volumeHud = true;
    emit hudChanged();
    if (m_pinVolume)
        return;
    const int gen = ++m_volumeGen;
    QTimer::singleShot(1600, this, [this, gen] {
        if (gen != m_volumeGen || m_pinVolume)
            return;
        m_volumeHud = false;
        emit hudChanged();
    });
}

void Guide::showToast(const QString &text)
{
    m_toastText = text;
    m_toastOn = true;
    emit toastChanged();
    if (m_pinToast)
        return;
    const int gen = ++m_toastGen;
    QTimer::singleShot(2200, this, [this, gen] {
        if (gen != m_toastGen || m_pinToast)
            return;
        m_toastOn = false;
        emit toastChanged();
    });
}

void Guide::publishFocus()
{
    syncFocusFields();
    emit focusChanged();
}

void Guide::syncFocusFields()
{
    const ShelfData *shelf = m_shelves->shelfAt(m_shelf);
    const RowData *row = currentRow();
    if (!row) {
        m_focusTitle = shelf ? shelf->title : m_brand.name;
        m_focusService.clear();
        m_focusShelf = shelf ? shelf->title : QString();
        m_focusArtwork.clear();
        m_focusAmbient.clear();
        m_focusColor = m_brand.accent;
        m_focusHref.clear();
        m_focusProgress = 0;
        m_focusLive = shelf && shelf->live;
        m_focusMirrored = false;
        m_focusSynopsis.clear();
        refreshSaved();
        return;
    }
    m_focusTitle = row->title;
    m_focusService = row->serviceLabel;
    m_focusShelf = shelf ? shelf->title : QString();
    m_focusArtwork = fileUrl(row->artwork);
    m_focusAmbient = fileUrl(row->ambient);
    m_focusColor = row->color.isEmpty() ? m_brand.accent : row->color;
    m_focusHref = row->href;
    m_focusProgress = row->progress;
    m_focusLive = row->live;
    m_focusSynopsis = row->synopsis;
    m_focusMirrored = !row->live
                      && (m_mirrorIds.contains(row->service) || row->href.startsWith(QStringLiteral("http://"))
                          || row->href.startsWith(QStringLiteral("https://")));
    refreshSaved();
}

void Guide::refreshSaved()
{
    const bool saved = m_savedKeys.contains(focusKey());
    if (saved == m_saved)
        return;
    m_saved = saved;
    emit savedChanged();
}

QString Guide::focusKey() const
{
    if (!m_focusHref.isEmpty())
        return m_focusHref;
    return m_focusTitle;
}

const RowData *Guide::currentRow() const
{
    const RowModel *rows = m_shelves->rowsAt(m_shelf);
    if (!rows)
        return nullptr;
    return rows->at(m_index);
}

} // namespace zappe
