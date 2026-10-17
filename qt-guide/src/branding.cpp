#include "branding.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace zappe {
namespace {

constexpr int kMaxNameChars = 40;
constexpr int kMaxTaglineChars = 80;

QString optString(const QJsonObject &obj, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const QJsonValue value = obj.value(QLatin1String(key));
        if (value.isString())
            return value.toString();
    }
    return {};
}

QString sanitizeText(const QString &raw, int maxChars)
{
    const QString value = raw.trimmed();
    if (value.isEmpty() || value.size() > maxChars)
        return {};
    return value;
}

QString sanitizeToken(const QString &raw, int maxChars)
{
    const QString value = raw.trimmed().toLower();
    if (value.isEmpty() || value.size() > maxChars)
        return {};
    for (const QChar ch : value) {
        const bool ok = ch.isLetterOrNumber() || ch == QLatin1Char('-') || ch == QLatin1Char('_');
        if (!ok || ch.unicode() > 127)
            return {};
    }
    return value;
}

bool isSafeColor(const QString &raw, QColor *out)
{
    const QString s = raw.trimmed();
    if (s.isEmpty() || s.size() > 80)
        return false;
    for (const QChar ch : s) {
        const char16_t c = ch.unicode();
        if (c == u'<' || c == u'>' || c == u';' || c == u'{' || c == u'}' || c == u'"' || c == u'\''
            || c == u'\\' || c == u'`')
            return false;
    }
    const QString lower = s.toLower();
    if (lower.contains(QStringLiteral("url(")) || lower.contains(QStringLiteral("expression"))
        || lower.contains(QStringLiteral("javascript")))
        return false;
    const QColor color = QColor::fromString(s);
    if (!color.isValid())
        return false;
    if (out)
        *out = color;
    return true;
}

QString resolveAsset(const QString &dataDir, const QString &spec)
{
    const QString trimmed = spec.trimmed();
    if (trimmed.isEmpty())
        return {};
    QString path = trimmed;
    if (path.startsWith(QStringLiteral("file:")))
        path = QUrl(path).toLocalFile();
    const QFileInfo info(path);
    const QString resolved = info.isAbsolute() ? info.absoluteFilePath() : QDir(dataDir).filePath(path);
    if (!QFileInfo::exists(resolved) || !QFileInfo(resolved).isFile()) {
        std::fprintf(stderr, "zappe-qt-guide: branding asset missing at %s\n", qPrintable(resolved));
        return {};
    }
    const QString ext = QFileInfo(resolved).suffix().toLower();
    if (ext != QLatin1String("png") && ext != QLatin1String("jpg") && ext != QLatin1String("jpeg")
        && ext != QLatin1String("webp") && ext != QLatin1String("gif")) {
        std::fprintf(stderr, "zappe-qt-guide: branding asset type not supported at %s\n",
                     qPrintable(resolved));
        return {};
    }
    if (QFileInfo(resolved).size() > 8 * 1024 * 1024) {
        std::fprintf(stderr, "zappe-qt-guide: branding asset too large at %s\n", qPrintable(resolved));
        return {};
    }
    return resolved;
}

} // namespace

QString inkFor(const QColor &color)
{
    const auto lin = [](double u) {
        return u <= 0.04045 ? u / 12.92 : std::pow((u + 0.055) / 1.055, 2.4);
    };
    const double L = 0.2126 * lin(color.redF()) + 0.7152 * lin(color.greenF()) + 0.0722 * lin(color.blueF());
    return L > 0.58 ? QStringLiteral("#1a140c") : QStringLiteral("#ffffff");
}

QString resolveDataDir()
{
    if (const QString explicitDir = qEnvironmentVariable("ZAPPE_DATA_DIR"); !explicitDir.isEmpty())
        return explicitDir;
    return QDir::homePath() + QStringLiteral("/.local/share/zappe");
}

Branding loadBranding(const QString &dataDir)
{
    Branding branding;
    const QString path = QDir(dataDir).filePath(QStringLiteral("branding.json"));
    if (!QFileInfo::exists(path))
        return branding;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "zappe-qt-guide: could not read %s; using Zappe\n", qPrintable(path));
        return branding;
    }
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        std::fprintf(stderr, "zappe-qt-guide: invalid branding.json (%s); using Zappe\n",
                     qPrintable(error.errorString()));
        return branding;
    }

    const QJsonObject obj = doc.object();
    branding.source = QStringLiteral("user");

    if (obj.contains(QStringLiteral("name"))) {
        const QString name = sanitizeText(obj.value(QStringLiteral("name")).toString(), kMaxNameChars);
        if (!name.isEmpty())
            branding.name = name;
        else
            std::fprintf(stderr, "zappe-qt-guide: branding name empty or too long; using Zappe\n");
    }

    const QString accentRaw = optString(obj, {"accent", "accentColor", "accent_color"});
    if (!accentRaw.trimmed().isEmpty()) {
        QColor parsed;
        if (isSafeColor(accentRaw, &parsed))
            branding.accent = accentRaw.trimmed();
        else
            std::fprintf(stderr, "zappe-qt-guide: branding accent rejected; using default\n");
    }

    if (obj.contains(QStringLiteral("tagline"))) {
        const QString tagline = sanitizeText(obj.value(QStringLiteral("tagline")).toString(), kMaxTaglineChars);
        if (!tagline.isEmpty())
            branding.tagline = tagline;
        else if (!obj.value(QStringLiteral("tagline")).toString().trimmed().isEmpty())
            std::fprintf(stderr, "zappe-qt-guide: branding tagline empty or too long; omitting\n");
    }

    branding.logoPath = resolveAsset(dataDir, optString(obj, {"logo"}));
    branding.splashPath = resolveAsset(dataDir, optString(obj, {"splash"}));

    if (const QJsonValue themeVal = obj.value(QStringLiteral("theme")); themeVal.isObject()) {
        const QJsonObject theme = themeVal.toObject();
        if (const QString style = sanitizeToken(optString(theme, {"style"}), 24); !style.isEmpty())
            branding.themeStyle = style;
        const QString background = optString(theme, {"background"}).trimmed();
        if (!background.isEmpty()) {
            QColor parsed;
            if (isSafeColor(background, &parsed))
                branding.background = background;
            else
                std::fprintf(stderr, "zappe-qt-guide: branding theme.background rejected; using default\n");
        }
        if (const QString focus = sanitizeToken(optString(theme, {"focusRing", "focus_ring"}), 32); !focus.isEmpty())
            branding.themeFocus = focus;
        const QString themeAccent = optString(theme, {"accent"}).trimmed();
        if (!themeAccent.isEmpty()) {
            QColor parsed;
            if (isSafeColor(themeAccent, &parsed))
                branding.accent = themeAccent;
        }
    }

    if (const QJsonValue idleVal = obj.value(QStringLiteral("idle")); idleVal.isObject()) {
        const QJsonObject idle = idleVal.toObject();
        if (const QString mode = sanitizeToken(optString(idle, {"mode"}), 24); !mode.isEmpty())
            branding.idleMode = mode == QLatin1String("off") ? QStringLiteral("off") : QStringLiteral("screensaver");
        if (const QString anim = sanitizeToken(optString(idle, {"animation"}), 32); !anim.isEmpty())
            branding.idleAnimation = anim;
        const QJsonValue timeout = idle.value(QStringLiteral("timeoutSeconds")).isUndefined()
                                       ? idle.value(QStringLiteral("timeout_seconds"))
                                       : idle.value(QStringLiteral("timeoutSeconds"));
        if (timeout.isDouble())
            branding.idleTimeoutSeconds = std::clamp(timeout.toInt(), 15, 3600);
        branding.idleAssetPath = resolveAsset(dataDir, optString(idle, {"asset"}));
    }

    branding.focusScale = branding.themeFocus == QLatin1String("subtle-scale") ? 1.08 : 1.12;
    QColor accent;
    if (!isSafeColor(branding.accent, &accent))
        accent = QColor(QStringLiteral("#E85A1B"));
    branding.accentInk = inkFor(accent);
    return branding;
}

} // namespace zappe
