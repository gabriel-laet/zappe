#include "ota.h"

#include "branding.h"

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
    // setsid deixa o shell como líder do grupo. Back/Escape no mpv saem por
    // input.conf; o app também manda TERM para os filhos (dvbv5-zap | mpv).
    const QString run = QStringLiteral(
                            "set -o pipefail; input=$(mktemp); "
                            "printf '%s\\n' 'ESC quit' 'XF86Back quit' 'XF86_Back quit' 'XF86_BACK quit' > \"$input\"; "
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

void OtaPlayer::stop()
{
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
