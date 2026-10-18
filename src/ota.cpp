#include "ota.h"

#include "branding.h"
#include "epg.h"
#include "posters.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>
#include <cstdio>

namespace zappe {
namespace {

QString foldName(const QString &name)
{
    QString out;
    for (const QChar ch : name) {
        switch (ch.unicode()) {
        case u'Á':
        case u'À':
        case u'Ã':
        case u'Â':
        case u'á':
        case u'à':
        case u'ã':
        case u'â':
            out.append(QLatin1Char('a'));
            break;
        case u'É':
        case u'Ê':
        case u'é':
        case u'ê':
            out.append(QLatin1Char('e'));
            break;
        case u'Í':
        case u'í':
            out.append(QLatin1Char('i'));
            break;
        case u'Ó':
        case u'Ô':
        case u'Õ':
        case u'ó':
        case u'ô':
        case u'õ':
            out.append(QLatin1Char('o'));
            break;
        case u'Ú':
        case u'Ü':
        case u'ú':
        case u'ü':
            out.append(QLatin1Char('u'));
            break;
        case u'Ç':
        case u'ç':
            out.append(QLatin1Char('c'));
            break;
        default:
            out.append(ch.toLower());
            break;
        }
    }
    return out.simplified();
}

bool is1Seg(const QString &name)
{
    const QString folded = foldName(name);
    return folded.contains(QStringLiteral("1seg")) || folded.contains(QStringLiteral("1-seg"))
           || folded.contains(QStringLiteral("one seg")) || folded.contains(QStringLiteral("oneseg"));
}

QString channelBaseKey(QString name)
{
    QString folded = foldName(name);
    folded.replace(QStringLiteral("1-seg"), QStringLiteral(" "));
    folded.replace(QStringLiteral("one seg"), QStringLiteral(" "));
    folded.replace(QStringLiteral("1seg"), QStringLiteral(" "));
    folded.replace(QStringLiteral("oneseg"), QStringLiteral(" "));
    folded = folded.simplified();
    const QStringList tokens = {QStringLiteral(" hdtv"), QStringLiteral(" hd"),  QStringLiteral(" fhd"),
                                QStringLiteral(" sd"),   QStringLiteral(" uhd"), QStringLiteral(" 4k"),
                                QStringLiteral(" (hd)"), QStringLiteral(" [hd]")};
    for (const QString &token : tokens) {
        if (folded.endsWith(token))
            folded.chop(token.size());
    }
    folded = folded.trimmed();
    for (const QString &prefix : {QStringLiteral("tv "), QStringLiteral("rede ")}) {
        if (folded.startsWith(prefix))
            folded = folded.mid(prefix.size()).trimmed();
    }
    return folded.trimmed();
}

int hdScore(const QString &name)
{
    const QString folded = foldName(name);
    if (folded.contains(QStringLiteral("hdtv")) || folded.contains(QStringLiteral(" fhd"))
        || folded.endsWith(QStringLiteral(" hd")) || folded.contains(QStringLiteral(" hd ")))
        return 0;
    if (folded.contains(QStringLiteral("hd")))
        return 1;
    if (folded.contains(QStringLiteral(" sd")))
        return 3;
    return 2;
}

QString numberInName(const QString &name)
{
    QString folded = foldName(name);
    folded.replace(QStringLiteral("1-seg"), QStringLiteral(" "));
    folded.replace(QStringLiteral("one seg"), QStringLiteral(" "));
    folded.replace(QStringLiteral("1seg"), QStringLiteral(" "));
    folded.replace(QStringLiteral("oneseg"), QStringLiteral(" "));
    folded = folded.simplified();
    static const QRegularExpression re(QStringLiteral("(?:^|\\D)(\\d{1,3}(?:\\.\\d)?)(?:\\D|$)"));
    const QRegularExpressionMatch match = re.match(folded);
    if (!match.hasMatch())
        return {};
    return match.captured(1);
}

struct VirtualChannel {
    int major = 0;
    int minor = -1;
    bool ok = false;
};

VirtualChannel parseVirtualChannel(const QString &text)
{
    const QString trimmed = text.trimmed();
    static const QRegularExpression re(QStringLiteral("^(\\d{1,3})(?:\\.(\\d))?$"));
    const QRegularExpressionMatch match = re.match(trimmed);
    VirtualChannel channel;
    if (!match.hasMatch())
        return channel;
    channel.major = match.captured(1).toInt();
    if (channel.major <= 0 || channel.major > 999)
        return {};
    channel.ok = true;
    if (match.lastCapturedIndex() >= 2 && !match.captured(2).isEmpty())
        channel.minor = match.captured(2).toInt();
    return channel;
}

QString formatVirtualChannel(const VirtualChannel &channel)
{
    if (!channel.ok)
        return {};
    if (channel.minor < 0)
        return QString::number(channel.major);
    return QStringLiteral("%1.%2").arg(channel.major).arg(channel.minor);
}

VirtualChannel defaultVirtualChannel(const QString &name)
{
    const QString id = networkMarkId(name);
    VirtualChannel channel;
    channel.ok = true;
    channel.minor = 1;
    if (id == QLatin1String("band"))
        channel.major = 13;
    else if (id == QLatin1String("record"))
        channel.major = 7;
    else if (id == QLatin1String("globo"))
        channel.major = 5;
    else if (id == QLatin1String("sbt"))
        channel.major = 4;
    else if (id == QLatin1String("cultura"))
        channel.major = 2;
    else if (id == QLatin1String("redetv"))
        channel.major = 9;
    else
        return {};
    return channel;
}

QString assignedNumber(const OtaService &service)
{
    if (!service.number.isEmpty()) {
        const VirtualChannel parsed = parseVirtualChannel(service.number);
        if (parsed.ok)
            return formatVirtualChannel(parsed);
    }
    return formatVirtualChannel(defaultVirtualChannel(service.name));
}

int scoreHit(const QString &key, const QString &folded, const QString &reqKey, const QString &reqFold)
{
    if (key == reqKey)
        return 0;
    if (folded == reqFold)
        return 1;
    if (folded.startsWith(reqFold) || key.startsWith(reqKey))
        return 2;
    return 3;
}

QString shellQuote(const QString &value)
{
    QString quoted = value;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

QStringList channelConfigPaths()
{
    const QString override = qEnvironmentVariable("ZAPPE_OTA_CHANNELS");
    if (!override.isEmpty()) {
        QStringList paths;
        for (const QString &part : override.split(QLatin1Char(':'))) {
            QString path = part.trimmed();
            if (path.isEmpty())
                continue;
            if (path.startsWith(QStringLiteral("~/")))
                path = QDir::homePath() + path.mid(1);
            paths.append(path);
        }
        return paths;
    }
    QStringList existing;
    const QStringList candidates = {QDir::homePath() + QStringLiteral("/tv/channels.conf"),
                                    QDir::homePath() + QStringLiteral("/.config/zappe/channels.conf"),
                                    resolveDataDir() + QStringLiteral("/channels.conf")};
    for (const QString &path : candidates) {
        if (QFile::exists(path))
            existing.append(path);
    }
    return existing;
}

} // namespace

QVector<OtaService> parseOtaServices(const QString &text)
{
    QVector<OtaService> out;
    OtaService current;
    bool inSection = false;
    const auto flush = [&] {
        if (!inSection || current.name.trimmed().isEmpty())
            return;
        if (current.layer == 1 || is1Seg(current.name))
            current.oneSeg = true;
        if (current.number.isEmpty())
            current.number = numberInName(current.name);
        out.append(current);
        current = {};
        inSection = false;
    };
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']')) && line.size() > 2) {
            flush();
            current.name = line.mid(1, line.size() - 2).trimmed();
            inSection = true;
            continue;
        }
        if (!inSection) {
            const int colon = line.indexOf(QLatin1Char(':'));
            if (colon > 0) {
                const QString name = line.left(colon).trimmed();
                if (!name.isEmpty() && !name.startsWith(QLatin1Char('['))) {
                    OtaService legacy;
                    legacy.name = name;
                    if (is1Seg(name))
                        legacy.oneSeg = true;
                    legacy.number = numberInName(name);
                    out.append(legacy);
                }
            }
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed().toUpper();
        const QString value = line.mid(eq + 1).trimmed();
        if (key == QLatin1String("LOGICAL_CHANNEL_NUMBER") || key == QLatin1String("VCHANNEL")
            || key == QLatin1String("LCN")) {
            if (!value.isEmpty())
                current.number = value;
        } else if (key == QLatin1String("ISDBT_LAYER_ENABLED")) {
            current.layer = value.toInt();
        }
    }
    flush();
    return out;
}

QStringList parseChannelNames(const QString &text)
{
    QStringList names;
    for (const OtaService &service : parseOtaServices(text))
        names.append(service.name);
    return names;
}

QStringList playableNames(const QVector<OtaService> &services)
{
    struct Group {
        QVector<OtaService> items;
    };
    QVector<Group> groups;
    QHash<QString, int> index;
    for (const OtaService &service : services) {
        if (service.name.trimmed().isEmpty())
            continue;
        const QString key = channelBaseKey(service.name);
        if (!index.contains(key)) {
            index.insert(key, groups.size());
            groups.append(Group{});
        }
        groups[index.value(key)].items.append(service);
    }
    QStringList out;
    for (const Group &group : groups) {
        bool full = false;
        for (const OtaService &item : group.items) {
            if (!item.oneSeg)
                full = true;
        }
        bool have = false;
        OtaService best;
        for (const OtaService &item : group.items) {
            if (full && item.oneSeg)
                continue;
            const bool better = !have || hdScore(item.name) < hdScore(best.name)
                                || (hdScore(item.name) == hdScore(best.name) && foldName(item.name) < foldName(best.name));
            if (better) {
                best = item;
                have = true;
            }
        }
        if (have)
            out.append(best.name);
    }
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) {
        const int score = hdScore(a) - hdScore(b);
        if (score != 0)
            return score < 0;
        return foldName(a) < foldName(b);
    });
    return out;
}

QStringList filterAndSortChannels(QStringList names)
{
    QVector<OtaService> services;
    services.reserve(names.size());
    for (const QString &name : names) {
        OtaService service;
        service.name = name;
        service.oneSeg = is1Seg(name);
        services.append(service);
    }
    return playableNames(services);
}

QString fullsegGapReason(const QVector<OtaService> &services, const QString &networkId)
{
    if (networkId != QLatin1String("band"))
        return {};
    QVector<OtaService> hits;
    for (const OtaService &service : services) {
        if (networkMarkId(service.name) == networkId)
            hits.append(service);
    }
    if (hits.isEmpty()) {
        return QStringLiteral(
            "A varredura não gravou a Band. O dvbv5 costuma travar na camada A (1-seg) ou o nome do SDT não contém Band; o canal físico usual é o 13.");
    }
    bool full = false;
    for (const OtaService &service : hits) {
        if (!service.oneSeg)
            full = true;
    }
    if (full)
        return {};
    return QStringLiteral(
        "A Band entrou só como 1-seg (camada A ou o nome traz 1seg). O fullseg não foi gravado; o canal continua na lista por essa versão.");
}

QVector<OtaChannel> otaChannelsFromConf(const QString &text)
{
    const QVector<OtaService> services = parseOtaServices(text);
    QHash<QString, OtaService> byName;
    for (const OtaService &service : services)
        byName.insert(service.name, service);
    QVector<OtaChannel> out;
    for (const QString &name : playableNames(services)) {
        const OtaService service = byName.value(name);
        OtaChannel channel;
        channel.name = name;
        channel.number = assignedNumber(service);
        channel.oneSeg = service.oneSeg;
        out.append(channel);
    }
    return out;
}

QString resolveDialTarget(const QString &typed, const QVector<OtaChannel> &channels)
{
    const VirtualChannel want = parseVirtualChannel(typed);
    if (!want.ok)
        return {};
    struct Hit {
        QString name;
        int rank = 9;
        int quality = 9;
        bool oneSeg = false;
        QString folded;
    };
    QVector<Hit> hits;
    for (const OtaChannel &channel : channels) {
        const VirtualChannel have = parseVirtualChannel(channel.number);
        if (!have.ok || have.major != want.major)
            continue;
        int rank = 9;
        if (want.minor < 0)
            rank = (have.minor == 1 || have.minor < 0) ? 0 : 1;
        else if (have.minor == want.minor)
            rank = 0;
        else if (have.minor < 0 && want.minor == 1)
            rank = 1;
        else
            continue;
        Hit hit;
        hit.name = channel.name;
        hit.rank = rank;
        hit.quality = hdScore(channel.name);
        hit.oneSeg = channel.oneSeg;
        hit.folded = foldName(channel.name);
        hits.append(hit);
    }
    if (hits.isEmpty())
        return {};
    std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
        if (a.rank != b.rank)
            return a.rank < b.rank;
        if (a.oneSeg != b.oneSeg)
            return !a.oneSeg;
        if (a.quality != b.quality)
            return a.quality < b.quality;
        if (a.folded.size() != b.folded.size())
            return a.folded.size() < b.folded.size();
        return a.folded < b.folded;
    });
    return hits.first().name;
}

QString appendDialText(const QString &current, const QString &glyph)
{
    if (glyph == QLatin1String(".")) {
        if (current.isEmpty() || current.contains(QLatin1Char('.')))
            return current;
        return current + glyph;
    }
    if (glyph.size() != 1 || !glyph.at(0).isDigit())
        return current;
    const int dot = current.indexOf(QLatin1Char('.'));
    if (dot < 0) {
        if (current.size() >= 3)
            return current;
        return current + glyph;
    }
    if (current.size() - dot - 1 >= 1)
        return current;
    return current + glyph;
}

int dialCommitMs()
{
    return 1300;
}

int otaFocusRefreshMs()
{
    return 450;
}

QString resolveChannelName(const QString &requested, const QStringList &names)
{
    const QString req = requested.trimmed();
    if (req.isEmpty())
        return {};
    const QStringList playable = filterAndSortChannels(names);
    if (playable.isEmpty())
        return {};
    for (const QString &name : playable) {
        if (name == req)
            return name;
    }
    const QString reqFold = foldName(req);
    for (const QString &name : playable) {
        if (foldName(name) == reqFold)
            return name;
    }
    const QString reqKey = channelBaseKey(req);
    QStringList ranked;
    for (const QString &name : playable) {
        const QString key = channelBaseKey(name);
        const QString folded = foldName(name);
        if (key == reqKey || (!reqKey.isEmpty() && reqKey.size() >= 3 && key.contains(reqKey))
            || (!reqFold.isEmpty() && folded.contains(reqFold))
            || (!reqFold.isEmpty() && reqFold.contains(folded)))
            ranked.append(name);
    }
    if (ranked.isEmpty())
        return {};
    std::sort(ranked.begin(), ranked.end(), [&](const QString &a, const QString &b) {
        const int hit = scoreHit(channelBaseKey(a), foldName(a), reqKey, reqFold)
                        - scoreHit(channelBaseKey(b), foldName(b), reqKey, reqFold);
        if (hit != 0)
            return hit < 0;
        const int score = hdScore(a) - hdScore(b);
        if (score != 0)
            return score < 0;
        return foldName(a) < foldName(b);
    });
    return ranked.first();
}

bool mpvPlaybackStarted(const QByteArray &ipcLine)
{
    const QByteArray trimmed = ipcLine.trimmed();
    if (trimmed.isEmpty())
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed);
    if (!doc.isObject())
        return false;
    const QJsonValue data = doc.object().value(QStringLiteral("data"));
    return data.isDouble() && data.toDouble() >= 0;
}

QString otaPipelineScript(const QString &conf, const QString &channel)
{
    // setsid deixa o shell como líder do grupo. O mpv chama XF86Back de BACK
    // (0.37) ou GO_BACK (0.38+); 0x21FF26 é o keysym quando ele não tem nome.
    // O app também captura o keycode X11 166 e manda TERM para os filhos.
    const QString run = QStringLiteral(
                            "set -o pipefail; input=$(mktemp); "
                            "printf '%s\\n' 'ESC quit' 'BACK quit' 'GO_BACK quit' '0x21FF26 quit' > \"$input\"; "
                            "trap 'pkill -TERM -P $$ >/dev/null 2>&1 || true' TERM INT; "
                            "dvbv5-zap -a 0 -c %1 -p %2 -r -o - | "
                            "mpv --hwdec=no --vo=gpu --demuxer-lavf-format=mpegts "
                            "--demuxer-lavf-analyzeduration=5 --cache=yes --fs --no-terminal "
                            "--input-conf=\"$input\" --input-ipc-server=%3 -; "
                            "status=$?; rm -f \"$input\"; exit $status")
                            .arg(shellQuote(conf), shellQuote(channel),
                                 shellQuote(QDir(resolveDataDir()).filePath(QStringLiteral("mpv.sock"))));
    return QStringLiteral("exec setsid sh -c %1").arg(shellQuote(run));
}

QString otaPreviewScript(const QString &conf, const QString &channel, const QString &tsPath, const QString &jpgPath)
{
    // Sem mpv e sem tela cheia: o guia continua na frente. -t solta o sintonizador.
    const QString run = QStringLiteral("set -o pipefail; dvbv5-zap -a 0 -c %1 -p %2 -r -t 8 -o %3; "
                                       "ffmpeg -y -i %3 -frames:v 1 -q:v 3 %4")
                            .arg(shellQuote(conf), shellQuote(channel), shellQuote(tsPath), shellQuote(jpgPath));
    return QStringLiteral("sh -c %1").arg(shellQuote(run));
}

FrameRefresh planFrameRefresh(bool playing, const QString &playingChannel, const QString &focused)
{
    if (playing && playingChannel == focused)
        return FrameRefresh::Screenshot;
    if (playing)
        return FrameRefresh::Busy;
    return FrameRefresh::Preview;
}

QVector<OtaChannel> listOtaChannels()
{
    QVector<OtaChannel> out;
    static QString logged;
    for (const QString &path : channelConfigPaths()) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QString text = QString::fromUtf8(file.readAll());
        const QVector<OtaService> services = parseOtaServices(text);
        QVector<OtaChannel> channels = otaChannelsFromConf(text);
        for (OtaChannel &channel : channels)
            channel.source = path;
        const QString stamp = path + QStringLiteral(":") + QString::number(services.size()) + QStringLiteral(":")
                              + QString::number(channels.size());
        if (logged != stamp) {
            logged = stamp;
            for (const OtaService &service : services) {
                bool kept = false;
                for (const OtaChannel &channel : channels) {
                    if (channel.name == service.name)
                        kept = true;
                }
                std::fprintf(stderr, "zappe: ota: %s %s%s\n", kept ? "fica" : "sai", qPrintable(service.name),
                             service.oneSeg ? " (1-seg)" : "");
            }
            const QString band = fullsegGapReason(services, QStringLiteral("band"));
            if (!band.isEmpty())
                std::fprintf(stderr, "zappe: ota: %s\n", qPrintable(band));
        }
        out += channels;
    }
    return out;
}

void mergeLiveChannels(CatalogFile &catalog, const QVector<OtaChannel> &channels)
{
    if (channels.isEmpty())
        return;
    ShelfData *live = nullptr;
    for (ShelfData &shelf : catalog.shelves) {
        if (shelf.id == QLatin1String("live") || shelf.id == QLatin1String("ao-vivo")) {
            live = &shelf;
            break;
        }
    }
    if (!live) {
        catalog.shelves.prepend(ShelfData{});
        live = &catalog.shelves.first();
        live->id = QStringLiteral("live");
    }
    live->title = QStringLiteral("Ao vivo");
    live->status = QStringLiteral("ok");
    live->live = true;
    live->rows.clear();
    for (const OtaChannel &channel : channels) {
        RowData row;
        row.title = channel.name;
        row.service = QStringLiteral("ota");
        row.serviceLabel = QStringLiteral("TV aberta");
        row.href = QStringLiteral("zappe://ota/") + channel.name;
        row.live = true;
        row.wide = true;
        if (!channel.number.isEmpty())
            row.synopsis = QStringLiteral("Canal %1").arg(channel.number);
        assignOtaFrame(row, resolveDataDir(), QDateTime::currentDateTime());
        live->rows.append(row);
    }
}

QString otaFramePath(const QString &dataDir, const QString &channel)
{
    const QByteArray hash = QCryptographicHash::hash(channel.toUtf8(), QCryptographicHash::Sha1).toHex();
    const QString dir = QDir(dataDir).filePath(QStringLiteral("ota-frames"));
    return QDir(dir).filePath(QString::fromLatin1(hash) + QStringLiteral(".jpg"));
}

bool otaFrameIsFresh(const QString &path, const QDateTime &now)
{
    const QFileInfo info(path);
    if (!info.exists() || info.size() < 32)
        return false;
    const qint64 age = info.lastModified().secsTo(now);
    return age >= -60 && age <= 12 * 60 * 60;
}

int otaFrameIntervalMs()
{
    return 3 * 60 * 1000;
}

void assignOtaFrame(RowData &row, const QString &dataDir, const QDateTime &now)
{
    if (!row.live || row.title.isEmpty())
        return;
    const QString path = otaFramePath(dataDir, row.title);
    if (otaFrameIsFresh(path, now)) {
        row.artworkSpec = path;
        row.frame = true;
    }
}

OtaPlayer::OtaPlayer(QObject *parent)
    : QObject(parent)
{
}

namespace {

void signalPipeline(qint64 pid, const QString &sig)
{
    if (pid <= 1)
        return;
    const QString id = QString::number(pid);
    QProcess::execute(QStringLiteral("pkill"), {sig, QStringLiteral("-P"), id});
    QProcess::execute(QStringLiteral("kill"), {sig, QStringLiteral("--"), QStringLiteral("-") + id});
}

} // namespace

void OtaPlayer::stopPreview()
{
    QProcess *process = m_preview;
    m_preview = nullptr;
    m_previewChannel.clear();
    if (!process)
        return;
    const qint64 pid = process->processId();
    signalPipeline(pid, QStringLiteral("-TERM"));
    process->kill();
    process->waitForFinished(200);
    process->deleteLater();
}

void OtaPlayer::stop()
{
    stopPreview();
    m_userStop = true;
    if (m_pictureTimer)
        m_pictureTimer->stop();
    if (m_frameTimer)
        m_frameTimer->stop();
    if (m_ipc) {
        m_ipc->abort();
        m_ipc->deleteLater();
        m_ipc = nullptr;
    }
    QProcess *process = m_process;
    if (!process) {
        m_channel.clear();
        return;
    }
    const qint64 pid = process->processId();
    signalPipeline(pid, QStringLiteral("-TERM"));
    process->terminate();
    if (m_process == process && !process->waitForFinished(1200)) {
        signalPipeline(pid, QStringLiteral("-KILL"));
        if (m_process == process)
            process->kill();
        if (m_process == process)
            process->waitForFinished(500);
    }
    if (m_process != process)
        return;
    m_process = nullptr;
    process->deleteLater();
    m_channel.clear();
    emit stopped();
}

void OtaPlayer::pollPicture()
{
    if (!m_process || m_sawPicture || m_socket.isEmpty())
        return;
    if (m_ipc) {
        m_ipc->abort();
        m_ipc->deleteLater();
    }
    m_ipc = new QLocalSocket(this);
    connect(m_ipc, &QLocalSocket::readyRead, this, [this] {
        if (!m_ipc || m_sawPicture)
            return;
        const QByteArray payload = m_ipc->readAll();
        for (const QByteArray &line : payload.split('\n')) {
            if (!mpvPlaybackStarted(line))
                continue;
            m_sawPicture = true;
            if (m_pictureTimer)
                m_pictureTimer->stop();
            emit started(m_channel);
            armFrames();
            return;
        }
    });
    connect(m_ipc, &QLocalSocket::connected, this, [this] {
        if (!m_ipc)
            return;
        m_ipc->write("{\"command\":[\"get_property\",\"playback-time\"]}\n");
    });
    m_ipc->connectToServer(m_socket);
}

void OtaPlayer::play(const QString &requested)
{
    stop();
    const QVector<OtaChannel> channels = listOtaChannels();
    if (channels.isEmpty()) {
        emit failed(QStringLiteral("channels.conf não encontrado."));
        return;
    }
    QStringList names;
    QString conf;
    for (const OtaChannel &channel : channels) {
        names.append(channel.name);
        if (conf.isEmpty())
            conf = channel.source;
    }
    const QString resolved = resolveChannelName(requested, names);
    if (resolved.isEmpty()) {
        emit failed(QStringLiteral("Canal não está no channels.conf: ") + requested);
        return;
    }
    for (const OtaChannel &channel : channels) {
        if (channel.name == resolved) {
            conf = channel.source;
            break;
        }
    }
    if (!QFile::exists(QStringLiteral("/dev/dvb/adapter0")) && !QFile::exists(QStringLiteral("/dev/dvb/adapter0/frontend0"))
        && qEnvironmentVariable("ZAPPE_OTA_FAKE").isEmpty()) {
        emit failed(QStringLiteral("Sem sintonizador em /dev/dvb/adapter0."));
        return;
    }
    m_socket = QDir(resolveDataDir()).filePath(QStringLiteral("mpv.sock"));
    QDir().mkpath(resolveDataDir());
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::ForwardedChannels);
    const QString script = qEnvironmentVariable("ZAPPE_OTA_FAKE").isEmpty()
                               ? otaPipelineScript(conf, resolved)
                               : QStringLiteral("exec sleep 30");
    process->start(QStringLiteral("sh"), {QStringLiteral("-c"), script});
    if (!process->waitForStarted(2000)) {
        process->deleteLater();
        emit failed(QStringLiteral("Não foi possível iniciar dvbv5-zap/mpv."));
        return;
    }
    m_process = process;
    m_channel = resolved;
    m_userStop = false;
    m_sawPicture = false;
    connect(process, &QProcess::finished, this, [this, process](int, QProcess::ExitStatus) {
        if (m_process != process)
            return;
        m_process = nullptr;
        const bool picture = m_sawPicture;
        const bool userStop = m_userStop;
        if (m_frameTimer)
            m_frameTimer->stop();
        m_channel.clear();
        process->deleteLater();
        if (!picture && !userStop)
            emit failed(QStringLiteral("A sintonia caiu antes da imagem."));
        else
            emit stopped();
    });
    if (!qEnvironmentVariable("ZAPPE_OTA_FAKE").isEmpty()) {
        m_sawPicture = true;
        emit started(resolved);
        return;
    }
    if (!m_pictureTimer) {
        m_pictureTimer = new QTimer(this);
        m_pictureTimer->setInterval(300);
        connect(m_pictureTimer, &QTimer::timeout, this, &OtaPlayer::pollPicture);
    }
    m_pictureTimer->start();
    pollPicture();
}

void OtaPlayer::armFrames()
{
    if (m_socket.isEmpty() || !m_process)
        return;
    if (!m_frameTimer) {
        m_frameTimer = new QTimer(this);
        m_frameTimer->setInterval(otaFrameIntervalMs());
        connect(m_frameTimer, &QTimer::timeout, this, &OtaPlayer::captureFrame);
    }
    m_frameTimer->start();
    QTimer::singleShot(8000, this, [this] {
        if (m_process && m_sawPicture)
            captureFrame();
    });
}

void OtaPlayer::captureFrame()
{
    if (!m_process || m_socket.isEmpty() || m_channel.isEmpty())
        return;
    const QString channel = m_channel;
    const QString path = otaFramePath(resolveDataDir(), channel);
    QDir().mkpath(QFileInfo(path).absolutePath());
    auto *sock = new QLocalSocket(this);
    connect(sock, &QLocalSocket::connected, sock, [sock, path] {
        QJsonArray command;
        command.append(QStringLiteral("screenshot-to-file"));
        command.append(path);
        command.append(QStringLiteral("video"));
        QJsonObject obj;
        obj.insert(QStringLiteral("command"), command);
        sock->write(QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n');
    });
    connect(sock, &QLocalSocket::readyRead, this, [this, sock, channel, path] {
        const QByteArray payload = sock->readAll();
        sock->disconnectFromServer();
        sock->deleteLater();
        if (!payload.contains("\"error\":\"success\""))
            return;
        if (!otaFrameIsFresh(path, QDateTime::currentDateTime()))
            return;
        emit frameReady(channel, path);
    });
    QTimer::singleShot(4000, sock, [sock] {
        if (sock->state() != QLocalSocket::UnconnectedState)
            sock->abort();
        sock->deleteLater();
    });
    sock->connectToServer(m_socket);
}

void OtaPlayer::refreshFocused(const QString &requested)
{
    const QVector<OtaChannel> channels = listOtaChannels();
    if (channels.isEmpty())
        return;
    QStringList names;
    QString conf;
    for (const OtaChannel &channel : channels) {
        names.append(channel.name);
        if (conf.isEmpty())
            conf = channel.source;
    }
    const QString resolved = resolveChannelName(requested, names);
    if (resolved.isEmpty())
        return;
    for (const OtaChannel &channel : channels) {
        if (channel.name == resolved && !channel.source.isEmpty()) {
            conf = channel.source;
            break;
        }
    }
    const FrameRefresh plan = planFrameRefresh(m_process != nullptr, m_channel, resolved);
    if (plan == FrameRefresh::Busy) {
        std::fprintf(stderr, "zappe: ota: sintonizador em \"%s\"; não interrompe para atualizar \"%s\"\n",
                     qPrintable(m_channel), qPrintable(resolved));
        return;
    }
    if (plan == FrameRefresh::Screenshot) {
        std::fprintf(stderr, "zappe: ota: capa de \"%s\" no canal que já está no ar\n", qPrintable(resolved));
        captureFrame();
        return;
    }
    if (m_preview && m_previewChannel == resolved)
        return;
    stopPreview();
    if (!QFile::exists(QStringLiteral("/dev/dvb/adapter0")) && !QFile::exists(QStringLiteral("/dev/dvb/adapter0/frontend0"))) {
        std::fprintf(stderr, "zappe: ota: sem sintonizador para a capa de \"%s\"\n", qPrintable(resolved));
        return;
    }
    if (!qEnvironmentVariable("ZAPPE_OTA_FAKE").isEmpty()) {
        std::fprintf(stderr, "zappe: ota: capa em segundo plano pulada (ZAPPE_OTA_FAKE)\n");
        return;
    }
    const QString data = resolveDataDir();
    QDir().mkpath(data);
    const QString tsPath = QDir(data).filePath(QStringLiteral("ota-preview.ts"));
    const QString jpgPath = otaFramePath(data, resolved);
    QDir().mkpath(QFileInfo(jpgPath).absolutePath());
    QFile::remove(tsPath);
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::ForwardedChannels);
    const QString script = otaPreviewScript(conf, resolved, tsPath, jpgPath);
    std::fprintf(stderr, "zappe: ota: lendo capa e EPG de \"%s\" sem abrir o player\n", qPrintable(resolved));
    process->start(QStringLiteral("sh"), {QStringLiteral("-c"), script});
    if (!process->waitForStarted(2000)) {
        std::fprintf(stderr, "zappe: ota: não abriu o zap de prévia\n");
        process->deleteLater();
        return;
    }
    m_preview = process;
    m_previewChannel = resolved;
    connect(process, &QProcess::finished, this, [this, process, resolved, tsPath, jpgPath](int, QProcess::ExitStatus) {
        const bool current = m_preview == process;
        if (current) {
            m_preview = nullptr;
            m_previewChannel.clear();
        }
        if (!current) {
            process->deleteLater();
            return;
        }
        QFile ts(tsPath);
        if (ts.open(QIODevice::ReadOnly)) {
            const QByteArray payload = ts.read(6 * 1024 * 1024);
            ts.close();
            const EpgSnapshot epg = parseEpgFromTs(payload);
            if (!epg.nowTitle.isEmpty() || !epg.nextTitle.isEmpty()) {
                std::fprintf(stderr, "zappe: ota: epg \"%s\" agora=\"%s\" a seguir=\"%s\"\n", qPrintable(resolved),
                             qPrintable(epg.nowTitle), qPrintable(epg.nextTitle));
                emit epgReady(resolved, epg.nowTitle, epg.nextTitle);
            }
        }
        if (otaFrameIsFresh(jpgPath, QDateTime::currentDateTime()))
            emit frameReady(resolved, jpgPath);
        process->deleteLater();
    });
}

void OtaPlayer::togglePause()
{
    if (m_socket.isEmpty() || !QFile::exists(m_socket))
        return;
    QProcess::startDetached(QStringLiteral("sh"),
                            {QStringLiteral("-c"),
                             QStringLiteral("printf '%s\\n' '{\"command\":[\"cycle\",\"pause\"]}' | socat - UNIX-CONNECT:%1")
                                 .arg(m_socket)});
}

} // namespace zappe
