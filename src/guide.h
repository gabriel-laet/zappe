#pragma once

#include "branding.h"
#include "catalog.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class QQuickWindow;

namespace zappe {

// Depois de republicar as fileiras, o foco volta ao mesmo href ou ao mesmo título.
// Fileira escondida nesta página não vira o hero. O índice não cai em 0 de outra fileira
// quando o título ainda está na tela.
bool restoreGuideFocus(const QVector<ShelfData> &shelves, const QVector<int> &shown, const QString &keepHref,
                       const QString &keepTitle, int keepShelf, int keepIndex, int &shelf, int &index);

class Guide : public QObject {
    Q_OBJECT
    Q_PROPERTY(ShelfModel *shelves READ shelves CONSTANT)
    Q_PROPERTY(int shelfCount READ shelfCount CONSTANT)
    Q_PROPERTY(int shelf READ shelf NOTIFY focusChanged)
    Q_PROPERTY(int index READ index NOTIFY focusChanged)
    Q_PROPERTY(bool detailOpen READ detailOpen NOTIFY detailChanged)
    Q_PROPERTY(int detailZone READ detailZone NOTIFY detailChanged)
    Q_PROPERTY(int detailButton READ detailButton NOTIFY detailChanged)
    Q_PROPERTY(int episode READ episode NOTIFY detailChanged)
    Q_PROPERTY(int episodeCount READ episodeCount CONSTANT)
    Q_PROPERTY(bool saved READ saved NOTIFY savedChanged)

    Q_PROPERTY(QString page READ page NOTIFY pageChanged)
    Q_PROPERTY(bool menuOpen READ menuOpen NOTIFY menuChanged)
    Q_PROPERTY(int menuIndex READ menuIndex NOTIFY menuChanged)
    Q_PROPERTY(int settingsIndex READ settingsIndex NOTIFY settingsChanged)
    Q_PROPERTY(QString resolution READ resolution NOTIFY settingsChanged)
    Q_PROPERTY(bool brandingOn READ brandingOn NOTIFY brandingChanged)
    Q_PROPERTY(bool brandCustom READ brandCustom CONSTANT)
    Q_PROPERTY(QString loadedBrandName READ loadedBrandName CONSTANT)

    Q_PROPERTY(QString focusTitle READ focusTitle NOTIFY focusChanged)
    Q_PROPERTY(QString focusService READ focusService NOTIFY focusChanged)
    Q_PROPERTY(QString focusShelf READ focusShelf NOTIFY focusChanged)
    Q_PROPERTY(QString focusArtwork READ focusArtwork NOTIFY focusChanged)
    Q_PROPERTY(QString focusAmbient READ focusAmbient NOTIFY focusChanged)
    Q_PROPERTY(QString focusColor READ focusColor NOTIFY focusChanged)
    Q_PROPERTY(QString focusHref READ focusHref NOTIFY focusChanged)
    Q_PROPERTY(qreal focusProgress READ focusProgress NOTIFY focusChanged)
    Q_PROPERTY(bool focusLive READ focusLive NOTIFY focusChanged)
    Q_PROPERTY(bool focusMirrored READ focusMirrored NOTIFY focusChanged)
    Q_PROPERTY(QString focusSynopsis READ focusSynopsis NOTIFY focusChanged)

    Q_PROPERTY(QString brandName READ brandName NOTIFY brandingChanged)
    Q_PROPERTY(QString accent READ accent NOTIFY brandingChanged)
    Q_PROPERTY(QString accentInk READ accentInk NOTIFY brandingChanged)
    Q_PROPERTY(QString background READ background NOTIFY brandingChanged)
    Q_PROPERTY(QString tagline READ tagline NOTIFY brandingChanged)
    Q_PROPERTY(QString logoUrl READ logoUrl NOTIFY brandingChanged)
    Q_PROPERTY(QString splashUrl READ splashUrl NOTIFY brandingChanged)
    Q_PROPERTY(qreal focusScale READ focusScale NOTIFY brandingChanged)
    Q_PROPERTY(QString shadowUrl READ shadowUrl NOTIFY brandingChanged)
    Q_PROPERTY(QString glowUrl READ glowUrl NOTIFY brandingChanged)
    Q_PROPERTY(QString uiFont READ uiFont CONSTANT)
    Q_PROPERTY(QString monoFont READ monoFont CONSTANT)
    Q_PROPERTY(QString noiseUrl READ noiseUrl CONSTANT)

    Q_PROPERTY(int volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY volumeChanged)
    Q_PROPERTY(bool volumeHud READ volumeHud NOTIFY hudChanged)
    Q_PROPERTY(QString toastText READ toastText NOTIFY toastChanged)
    Q_PROPERTY(bool toastOn READ toastOn NOTIFY toastChanged)
    Q_PROPERTY(bool cardDown READ cardDown NOTIFY cardDownChanged)
    Q_PROPERTY(int layoutEpoch READ layoutEpoch NOTIFY layoutChanged)
    Q_PROPERTY(QString mirrorService READ mirrorService NOTIFY layoutChanged)

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
    void setPreview(bool openMenu, const QString &page, bool pinVolume, const QString &toast);
    void attachWindow(QQuickWindow *window);
    void setApplianceDir(const QString &dir);
    void setCoalesceInput(bool on);
    Q_INVOKABLE void toast(const QString &text);

    ShelfModel *shelves() const { return m_shelves; }
    int shelfCount() const;
    int shelf() const { return m_shelf; }
    int index() const { return m_index; }
    bool detailOpen() const { return m_detailOpen; }
    int detailZone() const { return m_detailZone; }
    int detailButton() const { return m_detailButton; }
    int episode() const { return m_episode; }
    int episodeCount() const { return 6; }
    bool saved() const { return m_saved; }

    QString page() const { return m_page; }
    bool menuOpen() const { return m_menuOpen; }
    int menuIndex() const { return m_menuIndex; }
    int settingsIndex() const { return m_settingsIndex; }
    QString resolution() const { return m_resolution; }
    bool brandingOn() const { return m_brandingOn; }
    bool brandCustom() const { return m_loaded.source == QLatin1String("user"); }
    QString loadedBrandName() const { return m_loaded.name; }

    QString focusTitle() const { return m_focusTitle; }
    QString focusService() const { return m_focusService; }
    QString focusShelf() const { return m_focusShelf; }
    QString focusArtwork() const { return m_focusArtwork; }
    QString focusAmbient() const { return m_focusAmbient; }
    QString focusColor() const { return m_focusColor; }
    QString focusHref() const { return m_focusHref; }
    qreal focusProgress() const { return m_focusProgress; }
    bool focusLive() const { return m_focusLive; }
    bool focusMirrored() const { return m_focusMirrored; }
    QString focusSynopsis() const { return m_focusSynopsis; }

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
    QString noiseUrl() const { return m_noiseUrl; }

    int volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    bool volumeHud() const { return m_volumeHud; }
    QString toastText() const { return m_toastText; }
    bool toastOn() const { return m_toastOn; }
    bool cardDown() const { return m_cardDown; }
    int layoutEpoch() const { return m_layoutEpoch; }
    QString mirrorService() const { return m_mirrorService; }

    bool showFps() const { return m_showFps; }
    int fps() const { return m_fps; }
    bool windowed() const { return m_windowed; }
    bool frameless() const { return m_frameless; }
    bool skipSplash() const { return m_skipSplash; }
    QString catalogPath() const { return m_catalogPath; }

    Q_INVOKABLE void move(int dShelf, int dIndex);
    Q_INVOKABLE void activate();
    Q_INVOKABLE void back();
    Q_INVOKABLE void toggleMenu();
    Q_INVOKABLE void adjustVolume(int delta);
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE bool shelfShown(int index) const;
    Q_INVOKABLE QString episodeTitle(int index) const;

    void openMirror(const QString &serviceId);
    void closeMirror();
    void setMirrorServiceIds(const QStringList &ids);
    void replaceServiceShelves(const QString &serviceId, QVector<ShelfData> shelves);
    void noteLiveFrame(const QString &href, const QString &artwork, const QString &ambient, const QString &color);

signals:
    void focusChanged();
    void detailChanged();
    void activated();
    void fpsChanged();
    void pageChanged();
    void menuChanged();
    void settingsChanged();
    // index: 0 resolução, 1 marca, 2 estado da TV, 3 força, 4 volume, 5 mudo, 6 HDMI.
    // verb: 0 ativa, -1 esquerda, 1 direita.
    void settingsCommand(int index, int verb);
    void brandingChanged();
    void savedChanged();
    void volumeChanged();
    void hudChanged();
    void toastChanged();
    void cardDownChanged();
    void playRequested(const QString &title, const QString &service, const QString &href);
    void serviceRequested(const QString &service);
    void profileRequested(const QString &service, const QString &name);
    void mirrorRetryRequested(const QString &service);
    void layoutChanged();

private:
    void publishFocus();
    void syncFocusFields();
    void refreshSaved();
    void applyBrand(const Branding &branding);
    void setPage(const QString &page);
    void setMenuIndex(int index);
    void clampFocusToPage();
    void clampIndex();
    void moveShelves(int dShelf, int dIndex);
    void moveDetail(int dShelf, int dIndex);
    void moveSettings(int dShelf, int dIndex);
    void activateDetail();
    void toggleSaved();
    void nudgeResolution(int direction);
    void setBrandingEnabled(bool on);
    void pulseCard();
    void publishShelves(bool resetFocus);
    bool localMirrorStandIn(const ShelfData *shelf) const;
    void revealVolume();
    void showToast(const QString &text);
    bool coalesce(const QString &key);
    QString focusKey() const;
    const RowData *currentRow() const;
    static int menuForPage(const QString &page);
    static QString pageForMenu(int index);

    ShelfModel *m_shelves = nullptr;
    Branding m_brand;
    Branding m_loaded;
    Branding m_plain;
    bool m_brandingOn = true;
    QString m_logoUrl;
    QString m_splashUrl;
    QString m_shadowUrl;
    QString m_glowUrl;
    QString m_uiFont;
    QString m_monoFont;
    QString m_noiseUrl;
    QString m_catalogPath;
    QVector<ShelfData> m_baseShelves;
    QVector<ShelfData> m_mirrorShelves;
    QString m_mirrorService;
    QSet<QString> m_mirrorIds;
    int m_layoutEpoch = 0;

    int m_shelf = 0;
    int m_index = 0;
    bool m_detailOpen = false;
    int m_detailZone = 0;
    int m_detailButton = 0;
    int m_episode = 0;
    QSet<QString> m_savedKeys;
    bool m_saved = false;

    QString m_page = QStringLiteral("home");
    bool m_menuOpen = false;
    int m_menuIndex = 0;
    int m_settingsIndex = 0;
    QString m_resolution = QStringLiteral("1080p");
    QString m_dataDir;
    bool m_coalesce = false;
    QHash<QString, qint64> m_coalesceAt;

    int m_volume = 40;
    bool m_muted = false;
    bool m_volumeHud = false;
    bool m_pinVolume = false;
    int m_volumeGen = 0;
    QString m_toastText;
    bool m_cardDown = false;
    bool m_toastOn = false;
    bool m_pinToast = false;
    int m_toastGen = 0;

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
    bool m_focusLive = false;
    bool m_focusMirrored = false;
    QString m_focusSynopsis;
};

QString cacheDir();
QString pickFont(const QStringList &prefs);

} // namespace zappe
