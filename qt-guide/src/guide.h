#pragma once

#include "branding.h"
#include "catalog.h"

#include <QObject>
#include <QString>

class QQuickWindow;

namespace zappe {

class Guide : public QObject {
    Q_OBJECT
    Q_PROPERTY(ShelfModel *shelves READ shelves CONSTANT)
    Q_PROPERTY(int shelfCount READ shelfCount CONSTANT)
    Q_PROPERTY(int shelf READ shelf NOTIFY focusChanged)
    Q_PROPERTY(int index READ index NOTIFY focusChanged)
    Q_PROPERTY(bool detailOpen READ detailOpen NOTIFY detailChanged)

    Q_PROPERTY(QString focusTitle READ focusTitle NOTIFY focusChanged)
    Q_PROPERTY(QString focusService READ focusService NOTIFY focusChanged)
    Q_PROPERTY(QString focusShelf READ focusShelf NOTIFY focusChanged)
    Q_PROPERTY(QString focusArtwork READ focusArtwork NOTIFY focusChanged)
    Q_PROPERTY(QString focusAmbient READ focusAmbient NOTIFY focusChanged)
    Q_PROPERTY(QString focusColor READ focusColor NOTIFY focusChanged)
    Q_PROPERTY(QString focusHref READ focusHref NOTIFY focusChanged)
    Q_PROPERTY(qreal focusProgress READ focusProgress NOTIFY focusChanged)

    Q_PROPERTY(QString brandName READ brandName CONSTANT)
    Q_PROPERTY(QString accent READ accent CONSTANT)
    Q_PROPERTY(QString accentInk READ accentInk CONSTANT)
    Q_PROPERTY(QString background READ background CONSTANT)
    Q_PROPERTY(QString tagline READ tagline CONSTANT)
    Q_PROPERTY(QString logoUrl READ logoUrl CONSTANT)
    Q_PROPERTY(QString splashUrl READ splashUrl CONSTANT)
    Q_PROPERTY(qreal focusScale READ focusScale CONSTANT)
    Q_PROPERTY(QString shadowUrl READ shadowUrl CONSTANT)
    Q_PROPERTY(QString glowUrl READ glowUrl CONSTANT)
    Q_PROPERTY(QString uiFont READ uiFont CONSTANT)
    Q_PROPERTY(QString monoFont READ monoFont CONSTANT)

    Q_PROPERTY(bool showFps READ showFps CONSTANT)
    Q_PROPERTY(int fps READ fps NOTIFY fpsChanged)
    Q_PROPERTY(bool windowed READ windowed CONSTANT)
    Q_PROPERTY(bool frameless READ frameless CONSTANT)
    Q_PROPERTY(bool skipSplash READ skipSplash CONSTANT)
    Q_PROPERTY(QString catalogPath READ catalogPath CONSTANT)

public:
    explicit Guide(QObject *parent = nullptr);

    void setBranding(const Branding &branding);
    void setCatalog(CatalogFile catalog);
    void setInitial(int shelf, int index, bool detail);
    void setChrome(bool windowed, bool frameless, bool skipSplash, bool showFps);
    void attachWindow(QQuickWindow *window);

    ShelfModel *shelves() const { return m_shelves; }
    int shelfCount() const;
    int shelf() const { return m_shelf; }
    int index() const { return m_index; }
    bool detailOpen() const { return m_detailOpen; }

    QString focusTitle() const { return m_focusTitle; }
    QString focusService() const { return m_focusService; }
    QString focusShelf() const { return m_focusShelf; }
    QString focusArtwork() const { return m_focusArtwork; }
    QString focusAmbient() const { return m_focusAmbient; }
    QString focusColor() const { return m_focusColor; }
    QString focusHref() const { return m_focusHref; }
    qreal focusProgress() const { return m_focusProgress; }

    QString brandName() const { return m_brand.name; }
    QString accent() const { return m_brand.accent; }
    QString accentInk() const { return m_brand.accentInk; }
    QString background() const { return m_brand.background; }
    QString tagline() const { return m_brand.tagline; }
    QString logoUrl() const { return m_logoUrl; }
    QString splashUrl() const { return m_splashUrl; }
    qreal focusScale() const { return m_brand.focusScale; }
    QString shadowUrl() const { return m_shadowUrl; }
    QString glowUrl() const { return m_glowUrl; }
    QString uiFont() const { return m_uiFont; }
    QString monoFont() const { return m_monoFont; }

    bool showFps() const { return m_showFps; }
    int fps() const { return m_fps; }
    bool windowed() const { return m_windowed; }
    bool frameless() const { return m_frameless; }
    bool skipSplash() const { return m_skipSplash; }
    QString catalogPath() const { return m_catalogPath; }

    Q_INVOKABLE void move(int dShelf, int dIndex);
    Q_INVOKABLE void activate();
    Q_INVOKABLE void back();

signals:
    void focusChanged();
    void detailChanged();
    void activated();
    void fpsChanged();

private:
    void publishFocus();
    void syncFocusFields();
    const RowData *currentRow() const;

    ShelfModel *m_shelves = nullptr;
    Branding m_brand;
    QString m_logoUrl;
    QString m_splashUrl;
    QString m_shadowUrl;
    QString m_glowUrl;
    QString m_uiFont;
    QString m_monoFont;
    QString m_catalogPath;

    int m_shelf = 0;
    int m_index = 0;
    bool m_detailOpen = false;
    bool m_windowed = false;
    bool m_frameless = false;
    bool m_skipSplash = false;
    bool m_showFps = false;
    int m_fps = 0;

    QString m_focusTitle;
    QString m_focusService;
    QString m_focusShelf;
    QString m_focusArtwork;
    QString m_focusAmbient;
    QString m_focusColor;
    QString m_focusHref;
    qreal m_focusProgress = 0;
};

QString cacheDir();
QString pickFont(const QStringList &prefs);

} // namespace zappe
