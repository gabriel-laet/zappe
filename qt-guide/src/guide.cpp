#include "guide.h"

#include "posters.h"
#include "util.h"

#include <QColor>
#include <QDir>
#include <QFontDatabase>
#include <QQuickWindow>
#include <QStandardPaths>

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
}

void Guide::setBranding(const Branding &branding)
{
    m_brand = branding;
    const QString dir = cacheDir();
    QColor accent = QColor::fromString(m_brand.accent);
    if (!accent.isValid())
        accent = QColor(QStringLiteral("#E85A1B"));
    m_brand.shadowPath = makeShadowSprite(dir);
    m_brand.glowPath = makeGlowSprite(dir, accent);
    if (!m_brand.logoPath.isEmpty())
        m_brand.logoMarkPath = makeCircleMark(dir, m_brand.logoPath, 256);
    m_logoUrl = fileUrl(m_brand.logoMarkPath);
    const QString splash = !m_brand.splashPath.isEmpty() ? m_brand.splashPath : m_brand.logoMarkPath;
    m_splashUrl = fileUrl(splash);
    m_shadowUrl = fileUrl(m_brand.shadowPath);
    m_glowUrl = fileUrl(m_brand.glowPath);
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
    const int rows = m_shelves->rowsAt(m_shelf) ? m_shelves->rowsAt(m_shelf)->rowCount() : 0;
    m_index = rows == 0 ? 0 : std::clamp(index, 0, rows - 1);
    m_detailOpen = detail && currentRow() != nullptr;
    syncFocusFields();
}

void Guide::setChrome(bool windowed, bool frameless, bool skipSplash, bool showFps)
{
    m_windowed = windowed;
    m_frameless = frameless;
    m_skipSplash = skipSplash;
    m_showFps = showFps;
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

void Guide::move(int dShelf, int dIndex)
{
    if (m_detailOpen || shelfCount() == 0)
        return;
    bool changed = false;
    if (dShelf != 0) {
        const int next = std::clamp(m_shelf + dShelf, 0, shelfCount() - 1);
        if (next != m_shelf) {
            m_shelf = next;
            const int rows = m_shelves->rowsAt(m_shelf) ? m_shelves->rowsAt(m_shelf)->rowCount() : 0;
            if (rows == 0)
                m_index = 0;
            else if (m_index >= rows)
                m_index = rows - 1;
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

void Guide::activate()
{
    if (m_detailOpen) {
        emit activated();
        return;
    }
    if (!currentRow())
        return;
    m_detailOpen = true;
    emit detailChanged();
    emit focusChanged();
}

void Guide::back()
{
    if (!m_detailOpen)
        return;
    m_detailOpen = false;
    emit detailChanged();
    emit focusChanged();
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
}

const RowData *Guide::currentRow() const
{
    const RowModel *rows = m_shelves->rowsAt(m_shelf);
    if (!rows)
        return nullptr;
    return rows->at(m_index);
}

} // namespace zappe
