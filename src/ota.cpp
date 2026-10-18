#include "ota.h"

#include "branding.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>

#include <algorithm>

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

QStringList parseChannelNames(const QString &text)
{
    QStringList names;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']')) && line.size() > 2) {
            names.append(line.mid(1, line.size() - 2).trimmed());
            continue;
        }
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon > 0) {
            const QString name = line.left(colon).trimmed();
            if (!name.isEmpty() && !name.startsWith(QLatin1Char('[')))
                names.append(name);
        }
    }
    return names;
}

QStringList filterAndSortChannels(QStringList names)
{
    QStringList filtered;
    for (const QString &name : names) {
        if (!is1Seg(name))
            filtered.append(name);
    }
    std::sort(filtered.begin(), filtered.end(), [](const QString &a, const QString &b) {
        const int score = hdScore(a) - hdScore(b);
        if (score != 0)
            return score < 0;
        return foldName(a) < foldName(b);
    });
    QHash<QString, QString> seen;
    QStringList order;
    for (const QString &name : filtered) {
        const QString key = channelBaseKey(name);
        if (!seen.contains(key)) {
            seen.insert(key, name);
            order.append(key);
        } else if (hdScore(name) < hdScore(seen.value(key))) {
            seen.insert(key, name);
        }
    }
    QStringList out;
    for (const QString &key : order)
        out.append(seen.value(key));
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) {
        const int score = hdScore(a) - hdScore(b);
        if (score != 0)
            return score < 0;
        return foldName(a) < foldName(b);
    });
    return out;
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

QString otaPipelineScript(const QString &conf, const QString &channel)
{
    return QStringLiteral("set -o pipefail; dvbv5-zap -a 0 -c %1 -p %2 -r -o - | "
                          "mpv --hwdec=no --vo=gpu --demuxer-lavf-format=mpegts "
                          "--demuxer-lavf-analyzeduration=5 --cache=yes --fs --no-terminal "
                          "--input-ipc-server=%3 -")
        .arg(shellQuote(conf), shellQuote(channel), shellQuote(QDir(resolveDataDir()).filePath(QStringLiteral("mpv.sock"))));
}

QVector<OtaChannel> listOtaChannels()
{
    QVector<OtaChannel> out;
    for (const QString &path : channelConfigPaths()) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QStringList names = filterAndSortChannels(parseChannelNames(QString::fromUtf8(file.readAll())));
        for (const QString &name : names)
            out.append({name, path});
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
        live->rows.append(row);
    }
}

OtaPlayer::OtaPlayer(QObject *parent)
    : QObject(parent)
{
}

void OtaPlayer::stop()
{
    if (!m_process)
        return;
    const qint64 pid = m_process->processId();
    m_process->terminate();
    if (!m_process->waitForFinished(400))
        m_process->kill();
    m_process->deleteLater();
    m_process = nullptr;
    if (pid > 0) {
        QProcess::execute(QStringLiteral("kill"), {QStringLiteral("-TERM"), QStringLiteral("-") + QString::number(pid)});
    }
    m_channel.clear();
    emit stopped();
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
    connect(process, &QProcess::finished, this, [this, process] {
        if (m_process != process)
            return;
        m_process = nullptr;
        m_channel.clear();
        process->deleteLater();
        emit stopped();
    });
    emit started(resolved);
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
