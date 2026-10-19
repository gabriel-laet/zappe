#include "learn.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

namespace zappe {
namespace {

QString opt(const QJsonObject &obj, const char *key)
{
    const QJsonValue value = obj.value(QLatin1String(key));
    return value.isString() ? value.toString() : QString();
}

QStringList stringList(const QJsonValue &value)
{
    QStringList out;
    for (const QJsonValue &item : value.toArray()) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty())
            out.append(text);
    }
    return out;
}

bool safeServiceId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[a-z0-9][a-z0-9_-]{0,40}$"));
    return re.match(id).hasMatch();
}

QString adapterFile(const QString &dataDir, const QString &id)
{
    return QDir(dataDir).filePath(QStringLiteral("adapters/%1.json").arg(id));
}

QString historyFile(const QString &dataDir, const QString &id, int version)
{
    return QDir(dataDir).filePath(QStringLiteral("adapters/history/%1/%2.json").arg(id).arg(version));
}

bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(bytes) != bytes.size())
        return false;
    return file.commit();
}

int fileVersion(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
    const int version = obj.value(QStringLiteral("version")).toInt(0);
    return version < 1 ? 1 : version;
}

QByteArray extractJsonObject(const QByteArray &raw)
{
    QString text = QString::fromUtf8(raw).trimmed();
    if (text.startsWith(QStringLiteral("```"))) {
        const int nl = text.indexOf(QLatin1Char('\n'));
        if (nl >= 0)
            text = text.mid(nl + 1);
        if (text.endsWith(QStringLiteral("```")))
            text.chop(3);
        text = text.trimmed();
    }
    const int start = text.indexOf(QLatin1Char('{'));
    const int end = text.lastIndexOf(QLatin1Char('}'));
    if (start < 0 || end <= start)
        return {};
    return text.mid(start, end - start + 1).toUtf8();
}

QJsonObject unwrapAdapter(const QByteArray &raw, int depth)
{
    if (depth > 3)
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(extractJsonObject(raw));
    if (!doc.isObject())
        return {};
    const QJsonObject obj = doc.object();
    const QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
    if (!choices.isEmpty()) {
        const QString content =
            choices.at(0).toObject().value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
        if (!content.isEmpty())
            return unwrapAdapter(content.toUtf8(), depth + 1);
    }
    const QJsonArray services = obj.value(QStringLiteral("services")).toArray();
    if (!services.isEmpty() && services.at(0).isObject())
        return services.at(0).toObject();
    return obj;
}

QString takeString(const QJsonObject &obj, const char *key, const QString &fallback)
{
    const QString value = opt(obj, key);
    return value.isEmpty() ? fallback : value;
}

MirrorAdapter parseLearnedAdapter(const QByteArray &response, const MirrorAdapter &base)
{
    const QJsonObject obj = unwrapAdapter(response, 0);
    MirrorAdapter out = base;
    if (obj.isEmpty()) {
        out.feeds.clear();
        return out;
    }
    out.id = base.id.isEmpty() ? opt(obj, "id") : base.id;
    out.name = takeString(obj, "name", base.name);
    out.homeUrl = takeString(obj, "homeUrl", base.homeUrl);
    out.searchUrl = takeString(obj, "searchUrl", base.searchUrl);
    if (base.kind == QLatin1String("netflix") || base.kind == QLatin1String("youtube"))
        out.kind = base.kind;
    else
        out.kind = takeString(obj, "kind", base.kind);
    out.rowSelector = takeString(obj, "rowSelector", base.rowSelector);
    out.rowTitleSelector = takeString(obj, "rowTitleSelector", base.rowTitleSelector);
    out.cardSelector = takeString(obj, "cardSelector", base.cardSelector);
    out.linkSelector = takeString(obj, "linkSelector", base.linkSelector);
    out.imageSelector = takeString(obj, "imageSelector", base.imageSelector);
    out.progressSelector = takeString(obj, "progressSelector", base.progressSelector);
    out.profileSelector = takeString(obj, "profileSelector", base.profileSelector);
    if (obj.contains(QStringLiteral("continueHints")))
        out.continueHints = stringList(obj.value(QStringLiteral("continueHints")));
    out.feeds.clear();
    if (obj.contains(QStringLiteral("feeds"))) {
        for (const QJsonValue &value : obj.value(QStringLiteral("feeds")).toArray()) {
            MirrorAdapter probe;
            QJsonObject wrapped;
            wrapped.insert(QStringLiteral("id"), out.id.isEmpty() ? QStringLiteral("learned") : out.id);
            wrapped.insert(QStringLiteral("homeUrl"),
                           out.homeUrl.isEmpty() ? QStringLiteral("https://example.test/") : out.homeUrl);
            wrapped.insert(QStringLiteral("feeds"), QJsonArray{value});
            if (mirrorReadAdapter(wrapped, probe) && !probe.feeds.isEmpty())
                out.feeds.append(probe.feeds.at(0));
        }
    }
    out.version = base.version;
    return out;
}

} // namespace

LlmConfig loadLlmConfig(const QString &dataDir)
{
    LlmConfig config;
    QFile file(QDir(dataDir).filePath(QStringLiteral("llm.json")));
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
        config.url = opt(obj, "url");
        config.key = opt(obj, "key");
        config.model = opt(obj, "model");
        config.fallbackModel = opt(obj, "fallbackModel");
        config.sessionHeader = opt(obj, "sessionHeader");
        config.userAgent = opt(obj, "userAgent");
    }
    const auto fromEnv = [](const char *name, QString &field) {
        const QByteArray env = qgetenv(name);
        if (!env.isEmpty())
            field = QString::fromUtf8(env);
    };
    fromEnv("ZAPPE_LLM_URL", config.url);
    fromEnv("ZAPPE_LLM_KEY", config.key);
    fromEnv("ZAPPE_LLM_MODEL", config.model);
    fromEnv("ZAPPE_LLM_FALLBACK_MODEL", config.fallbackModel);
    fromEnv("ZAPPE_LLM_SESSION_HEADER", config.sessionHeader);
    fromEnv("ZAPPE_LLM_USER_AGENT", config.userAgent);
    if (config.model.trimmed().isEmpty())
        config.model = QStringLiteral("gpt-4o-mini");
    config.url = config.url.trimmed();
    config.key = config.key.trimmed();
    config.model = config.model.trimmed();
    config.fallbackModel = config.fallbackModel.trimmed();
    config.sessionHeader = config.sessionHeader.trimmed();
    config.userAgent = config.userAgent.trimmed();
    return config;
}

bool llmEnabled(const LlmConfig &config)
{
    return !config.url.isEmpty();
}

QByteArray llmAuthorization(const LlmConfig &config)
{
    if (config.key.isEmpty())
        return {};
    return QByteArrayLiteral("Bearer ") + config.key.toUtf8();
}

QString llmUserAgent(const LlmConfig &config)
{
    const QString custom = config.userAgent.trimmed();
    if (!custom.isEmpty())
        return custom;
    QString version = QCoreApplication::applicationVersion().trimmed();
    if (version.isEmpty())
        version = QStringLiteral("0.2.0");
    return QStringLiteral("zappe/") + version;
}

QString llmSessionId(const QString &serviceId, const QDate &day)
{
    QString id;
    const QString raw = serviceId.trimmed().toLower();
    for (const QChar c : raw) {
        if (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_'))
            id.append(c);
    }
    if (id.isEmpty())
        id = QStringLiteral("catalog");
    const QDate when = day.isValid() ? day : QDate::currentDate();
    return QStringLiteral("zappe-%1-%2").arg(id, when.toString(QStringLiteral("yyyyMMdd")));
}

QVector<LlmHeader> llmRequestHeaders(const LlmConfig &config, const QString &serviceId, const QDate &day)
{
    QVector<LlmHeader> headers;
    headers.append({QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")});
    headers.append({QByteArrayLiteral("User-Agent"), llmUserAgent(config).toUtf8()});
    const QByteArray authorization = llmAuthorization(config);
    if (!authorization.isEmpty())
        headers.append({QByteArrayLiteral("Authorization"), authorization});
    QString sessionName;
    for (const QChar c : config.sessionHeader.trimmed()) {
        if (c.unicode() >= 33 && c.unicode() < 127 && c != QLatin1Char(':'))
            sessionName.append(c);
    }
    if (!sessionName.isEmpty())
        headers.append({sessionName.toUtf8(), llmSessionId(serviceId, day).toUtf8()});
    return headers;
}

bool llmUseFallback(bool requestFailed, bool adapterRejected, bool alreadyTriedFallback, const QString &fallbackModel,
                    const QString &usedModel)
{
    if (alreadyTriedFallback)
        return false;
    const QString next = fallbackModel.trimmed();
    if (next.isEmpty() || next == usedModel.trimmed())
        return false;
    return requestFailed || adapterRejected;
}

QString llmResultLog(const QString &serviceId, const QString &model, int httpStatus, qint64 elapsedMs)
{
    if (elapsedMs < 0)
        elapsedMs = 0;
    return QStringLiteral("zappe: llm %1 model %2 http %3 %4 ms")
        .arg(serviceId.left(40), model.left(80), QString::number(httpStatus), QString::number(elapsedMs));
}

QByteArray llmRepairBody(const MirrorAdapter &current, const QString &dom, const QVector<NetworkSample> &samples,
                         const QByteArray &screenshot, const QString &model)
{
    QString text = QStringLiteral(
        "A extração não achou fileiras com títulos reproduzíveis. Repare o adaptador usando as amostras. "
        "Responda só com o objeto JSON do serviço, incluindo feeds.\nAdaptador atual:\n");
    text += QString::fromUtf8(mirrorAdapterJson(current));
    text += QStringLiteral("\nDOM:\n");
    text += dom.left(8000);
    int added = 0;
    for (const NetworkSample &sample : samples) {
        if (added >= 4)
            break;
        text += QStringLiteral("\nURL: ");
        text += sample.url;
        text += QLatin1Char('\n');
        text += QString::fromUtf8(sample.body.left(24 * 1024));
        ++added;
    }
    if (!screenshot.isEmpty() && screenshot.size() > 80000)
        text += QStringLiteral("\nscreenshot: %1 bytes\n").arg(screenshot.size());

    const QString usedModel = model.trimmed().isEmpty() ? QStringLiteral("gpt-4o-mini") : model.trimmed();
    QJsonObject system;
    system.insert(QStringLiteral("role"), QStringLiteral("system"));
    system.insert(QStringLiteral("content"),
                  QStringLiteral("Você repara um adaptador declarativo do Zappe. Responda somente com um objeto JSON, "
                                 "sem markdown. O objeto tem id, kind, homeUrl e feeds. feeds é obrigatório. Cada feed "
                                 "tem urlContains, titleKeys, hrefKeys, artworkKeys, shelfKeys e hrefTemplate. "
                                 "urlContains deve aparecer na URL da amostra. hrefTemplate usa {href} quando o valor "
                                 "não é http. As chaves precisam existir nas amostras e extrair títulos, capas http e "
                                 "links de reprodução. Não invente segredo, chave ou token."));
    QJsonObject user;
    user.insert(QStringLiteral("role"), QStringLiteral("user"));
    if (!screenshot.isEmpty() && screenshot.size() <= 80000) {
        QJsonArray parts;
        QJsonObject textPart;
        textPart.insert(QStringLiteral("type"), QStringLiteral("text"));
        textPart.insert(QStringLiteral("text"), text);
        QJsonObject imageUrl;
        imageUrl.insert(QStringLiteral("url"),
                        QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(screenshot.toBase64()));
        QJsonObject imagePart;
        imagePart.insert(QStringLiteral("type"), QStringLiteral("image_url"));
        imagePart.insert(QStringLiteral("image_url"), imageUrl);
        parts.append(textPart);
        parts.append(imagePart);
        user.insert(QStringLiteral("content"), parts);
    } else {
        user.insert(QStringLiteral("content"), text);
    }
    QJsonArray messages;
    messages.append(system);
    messages.append(user);
    QJsonObject root;
    root.insert(QStringLiteral("model"), usedModel);
    root.insert(QStringLiteral("temperature"), 0.1);
    root.insert(QStringLiteral("messages"), messages);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool validateLearnedAdapter(const MirrorAdapter &candidate, const QVector<NetworkSample> &samples, int minTitles)
{
    if (candidate.id.isEmpty() || candidate.feeds.isEmpty() || samples.isEmpty() || minTitles < 1)
        return false;
    return mirrorPageUsable(parseMirrorFeeds(samples, candidate), minTitles);
}

bool saveLearnedAdapter(const QString &dataDir, MirrorAdapter &adapter)
{
    if (dataDir.isEmpty() || !safeServiceId(adapter.id))
        return false;
    const QString path = adapterFile(dataDir, adapter.id);
    if (QFile::exists(path)) {
        const int version = fileVersion(path);
        QFile current(path);
        if (!current.open(QIODevice::ReadOnly))
            return false;
        const QByteArray previous = current.readAll();
        current.close();
        if (previous.isEmpty() || !writeBytes(historyFile(dataDir, adapter.id, version), previous))
            return false;
        adapter.version = version + 1;
    } else {
        adapter.version = 1;
    }
    QJsonObject obj = QJsonDocument::fromJson(mirrorAdapterJson(adapter)).object();
    obj.insert(QStringLiteral("version"), adapter.version);
    return writeBytes(path, QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

bool rollbackLearnedAdapter(const QString &dataDir, const QString &serviceId)
{
    if (dataDir.isEmpty() || !safeServiceId(serviceId))
        return false;
    const QString path = adapterFile(dataDir, serviceId);
    if (!QFile::exists(path))
        return false;
    const int current = fileVersion(path);
    const QDir history(QDir(dataDir).filePath(QStringLiteral("adapters/history/%1").arg(serviceId)));
    int best = -1;
    const QStringList names = history.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &name : names) {
        bool ok = false;
        const int version = QFileInfo(name).baseName().toInt(&ok);
        if (ok && version < current && version > best)
            best = version;
    }
    if (best < 1)
        return false;
    QFile previous(historyFile(dataDir, serviceId, best));
    if (!previous.open(QIODevice::ReadOnly))
        return false;
    const QByteArray bytes = previous.readAll();
    previous.close();
    if (bytes.isEmpty())
        return false;
    return writeBytes(path, bytes);
}

bool applyLearnResponse(const QString &dataDir, const MirrorAdapter &base, const QVector<NetworkSample> &samples,
                        const QByteArray &response)
{
    MirrorAdapter candidate = parseLearnedAdapter(response, base);
    if (!validateLearnedAdapter(candidate, samples, 4))
        return false;
    return saveLearnedAdapter(dataDir, candidate);
}

} // namespace zappe
