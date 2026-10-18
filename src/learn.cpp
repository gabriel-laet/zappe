#include "learn.h"

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
    }
    const QByteArray envUrl = qgetenv("ZAPPE_LLM_URL");
    if (!envUrl.isEmpty())
        config.url = QString::fromUtf8(envUrl);
    const QByteArray envKey = qgetenv("ZAPPE_LLM_KEY");
    if (!envKey.isEmpty())
        config.key = QString::fromUtf8(envKey);
    const QByteArray envModel = qgetenv("ZAPPE_LLM_MODEL");
    if (!envModel.isEmpty())
        config.model = QString::fromUtf8(envModel);
    if (config.model.trimmed().isEmpty())
        config.model = QStringLiteral("gpt-4o-mini");
    config.url = config.url.trimmed();
    config.key = config.key.trimmed();
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
