#include "guide.h"

#include "posters.h"
#include "util.h"

#include <QColor>
#include <QDir>
#include <QFontDatabase>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace zappe {

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
    m_shelves->setShelves(std::move(catalog.shelves));
    m_shelf = 0;
    m_index = 0;
    m_detailOpen = false;
    syncFocusFields();
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
    if (!shelf)
        return false;
    if (m_page == QLatin1String("live"))
        return shelf->live;
    if (m_page == QLatin1String("streaming"))
        return !shelf->live;
    if (m_page == QLatin1String("settings"))
        return false;
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
    if (page == m_page)
        return;
    m_page = page;
    m_menuIndex = menuForPage(page);
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
    if (dShelf > 0 && m_detailZone == 0) {
        m_detailZone = 1;
        changed = true;
    } else if (dShelf < 0 && m_detailZone == 1) {
        m_detailZone = 0;
        changed = true;
    }
    if (dIndex != 0 && m_detailZone == 0) {
        const int next = std::clamp(m_detailButton + dIndex, 0, 2);
        if (next != m_detailButton) {
            m_detailButton = next;
            changed = true;
        }
    } else if (dIndex != 0 && m_detailZone == 1) {
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
    if (dShelf != 0) {
        const int next = std::clamp(m_settingsIndex + dShelf, 0, 1);
        if (next != m_settingsIndex) {
            m_settingsIndex = next;
            emit settingsChanged();
        }
    }
    if (dIndex != 0) {
        if (m_settingsIndex == 0)
            nudgeResolution(dIndex);
        else
            setBrandingEnabled(dIndex > 0 ? true : false);
    }
}

void Guide::nudgeResolution(int direction)
{
    const QString next = direction > 0 ? QStringLiteral("4K") : QStringLiteral("1080p");
    if (next == m_resolution)
        return;
    m_resolution = next;
    emit settingsChanged();
    showToast(m_resolution == QLatin1String("4K") ? QStringLiteral("Resolução 4K — só na interface")
                                                  : QStringLiteral("Resolução 1080p"));
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
    if (m_menuOpen) {
        m_menuOpen = false;
        emit menuChanged();
        return;
    }
    if (m_page == QLatin1String("settings") && !m_detailOpen) {
        if (m_settingsIndex == 0)
            nudgeResolution(m_resolution == QLatin1String("1080p") ? 1 : -1);
        else
            setBrandingEnabled(!m_brandingOn);
        return;
    }
    if (m_detailOpen) {
        activateDetail();
        return;
    }
    if (!currentRow())
        return;
    m_detailOpen = true;
    m_detailZone = 0;
    m_detailButton = 0;
    m_episode = 0;
    refreshSaved();
    emit detailChanged();
    emit focusChanged();
}

void Guide::activateDetail()
{
    if (m_detailZone == 0 && m_detailButton == 0)
        showToast(QStringLiteral("Assistir · ") + m_focusTitle);
    else if (m_detailZone == 0 && m_detailButton == 1)
        showToast(QStringLiteral("Trailer · ") + m_focusTitle);
    else if (m_detailZone == 0 && m_detailButton == 2)
        toggleSaved();
    else
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

void Guide::back()
{
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
    if (m_page != QLatin1String("home")) {
        setPage(QStringLiteral("home"));
        return;
    }
}

void Guide::toggleMenu()
{
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
