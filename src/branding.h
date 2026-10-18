#pragma once

#include <QColor>
#include <QString>

namespace zappe {

// Runtime branding from `$ZAPPE_DATA_DIR/branding.json` or
// `~/.local/share/zappe/branding.json`. Same shape as
// `packaging/appliance/examples/branding.json`.
// Missing or invalid files fall back to the generic name "Zappe".

struct Branding {
    QString name = QStringLiteral("Zappe");
    QString accent = QStringLiteral("#E85A1B");
    QString background = QStringLiteral("#000000");
    QString tagline;
    QString logoPath;
    QString splashPath;
    QString idleAssetPath;
    QString idleMode = QStringLiteral("screensaver");
    int idleTimeoutSeconds = 120;
    QString idleAnimation = QStringLiteral("soft-breathe");
    QString themeStyle = QStringLiteral("apple-tv");
    QString themeFocus = QStringLiteral("subtle-scale");
    QString source = QStringLiteral("default");
    qreal focusScale = 1.08;
    QString accentInk = QStringLiteral("#ffffff");

    // Filled by prepareBrandingAssets(), not by the JSON parser.
    QString logoMarkPath;
    QString glowPath;
    QString shadowPath;
};

QString resolveDataDir();
Branding loadBranding(const QString &dataDir);
QString inkFor(const QColor &color);

} // namespace zappe
