#include "samsung.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSslError>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>

#include <cstdio>

#ifdef Q_OS_UNIX
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <memory>

namespace zappe {
namespace {

QString envOr(const char *name, const QString &fallback)
{
    const QString value = qEnvironmentVariable(name).trimmed();
    return value.isEmpty() ? fallback : value;
}

bool envOff(const char *name)
{
    const QString value = qEnvironmentVariable(name).trimmed().toLower();
    return value == QLatin1String("1") || value == QLatin1String("true") || value == QLatin1String("yes")
           || value == QLatin1String("on");
}

QJsonValue powerValue(const QJsonObject &root, const QJsonObject &device)
{
    if (device.contains(QStringLiteral("PowerState")))
        return device.value(QStringLiteral("PowerState"));
    if (device.contains(QStringLiteral("powerState")))
        return device.value(QStringLiteral("powerState"));
    if (root.contains(QStringLiteral("PowerState")))
        return root.value(QStringLiteral("PowerState"));
    return {};
}

} // namespace

const char *tvKeyCode(TvKey key)
{
    switch (key) {
    case TvKey::VolumeUp:
        return "KEY_VOLUP";
    case TvKey::VolumeDown:
        return "KEY_VOLDOWN";
    case TvKey::Mute:
        return "KEY_MUTE";
    case TvKey::Power:
        return "KEY_POWER";
    case TvKey::Hdmi1:
        return "KEY_HDMI1";
    case TvKey::Hdmi2:
        return "KEY_HDMI2";
    case TvKey::Hdmi3:
        return "KEY_HDMI3";
    case TvKey::Hdmi4:
        return "KEY_HDMI4";
    }
    return "KEY_VOLUP";
}

SamsungConfig loadSamsungConfig(const QString &dataDir)
{
    SamsungConfig config;
    config.host = envOr("ZAPPE_SAMSUNG_HOST", config.host);
    config.name = envOr("ZAPPE_SAMSUNG_NAME", config.name);
    config.mac = envOr("ZAPPE_SAMSUNG_MAC", QString());
    config.cec = envOr("ZAPPE_SAMSUNG_CEC", QString());
    if (const QString port = qEnvironmentVariable("ZAPPE_SAMSUNG_PORT"); !port.isEmpty())
        config.port = port.toInt();
    if (const QString port = qEnvironmentVariable("ZAPPE_SAMSUNG_SECURE_PORT"); !port.isEmpty())
        config.securePort = port.toInt();
    if (const QString token = qEnvironmentVariable("ZAPPE_SAMSUNG_TOKEN"); !token.isEmpty())
        config.token = token.trimmed();

    QFile file(dataDir + QStringLiteral("/samsung.json"));
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isObject()) {
            const QJsonObject obj = doc.object();
            if (qEnvironmentVariableIsEmpty("ZAPPE_SAMSUNG_HOST")) {
                const QString host = obj.value(QStringLiteral("host")).toString().trimmed();
                if (!host.isEmpty())
                    config.host = host;
            }
            if (qEnvironmentVariableIsEmpty("ZAPPE_SAMSUNG_PORT") && obj.contains(QStringLiteral("port")))
                config.port = obj.value(QStringLiteral("port")).toInt(config.port);
            const QJsonValue secure = obj.contains(QStringLiteral("securePort"))
                                          ? obj.value(QStringLiteral("securePort"))
                                          : obj.value(QStringLiteral("secure_port"));
            if (qEnvironmentVariableIsEmpty("ZAPPE_SAMSUNG_SECURE_PORT") && secure.isDouble())
                config.securePort = secure.toInt();
            if (config.token.isEmpty())
                config.token = obj.value(QStringLiteral("token")).toString().trimmed();
            if (qEnvironmentVariableIsEmpty("ZAPPE_SAMSUNG_NAME")) {
                const QString name = obj.value(QStringLiteral("name")).toString().trimmed();
                if (!name.isEmpty())
                    config.name = name;
            }
            if (qEnvironmentVariableIsEmpty("ZAPPE_SAMSUNG_MAC")) {
                const QString mac = obj.value(QStringLiteral("mac")).toString().trimmed();
                if (!mac.isEmpty())
                    config.mac = mac;
            }
            if (qEnvironmentVariableIsEmpty("ZAPPE_SAMSUNG_CEC") && obj.contains(QStringLiteral("cec")))
                config.cec = obj.value(QStringLiteral("cec")).toString().trimmed();
        }
    }
    config.disabled = envOff("ZAPPE_SAMSUNG_DISABLE") || config.host.trimmed().isEmpty();
    return config;
}

QString samsungWebsocketUrl(const SamsungConfig &config, bool secure)
{
    const QByteArray name = config.name.toUtf8().toBase64();
    QString url = QStringLiteral("%1://%2:%3/api/v2/channels/samsung.remote.control?name=%4")
                      .arg(secure ? QStringLiteral("wss") : QStringLiteral("ws"), config.host,
                           QString::number(secure ? config.securePort : config.port), QString::fromLatin1(name));
    if (!config.token.trimmed().isEmpty())
        url += QStringLiteral("&token=") + config.token.trimmed();
    return url;
}

QString samsungConnectToken(const QByteArray &message)
{
    const QJsonDocument doc = QJsonDocument::fromJson(message);
    if (!doc.isObject())
        return {};
    const QJsonObject obj = doc.object();
    if (obj.value(QStringLiteral("event")).toString() != QLatin1String("ms.channel.connect"))
        return {};
    return obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("token")).toString().trimmed();
}

bool samsungAuthDenied(const QByteArray &message)
{
    const QJsonDocument doc = QJsonDocument::fromJson(message);
    if (!doc.isObject())
        return false;
    const QString event = doc.object().value(QStringLiteral("event")).toString();
    return event == QLatin1String("ms.channel.unauthorized") || event == QLatin1String("ms.channel.timeOut");
}

int samsungPairingWaitMs()
{
    return 45000;
}

bool samsungUpgradeToSecure(bool secure, bool denied)
{
    return !secure && denied;
}

bool samsungRetryWithoutToken(bool secure, bool hadToken, bool stripped, bool denied)
{
    return secure && hadToken && !stripped && denied;
}

bool samsungShouldHoldPairing(bool secure, bool tokenPresent, bool denied)
{
    return secure && !tokenPresent && denied;
}

bool saveSamsungToken(const QString &dataDir, const QString &host, const QString &name, const QString &token)
{
    const QString trimmed = token.trimmed();
    if (dataDir.trimmed().isEmpty() || trimmed.isEmpty())
        return false;
    QDir().mkpath(dataDir);
    QJsonObject obj;
    QFile existing(dataDir + QStringLiteral("/samsung.json"));
    if (existing.open(QIODevice::ReadOnly)) {
        const QJsonDocument current = QJsonDocument::fromJson(existing.readAll());
        if (current.isObject())
            obj = current.object();
        existing.close();
    }
    if (!host.trimmed().isEmpty())
        obj.insert(QStringLiteral("host"), host.trimmed());
    if (!name.trimmed().isEmpty())
        obj.insert(QStringLiteral("name"), name.trimmed());
    obj.insert(QStringLiteral("token"), trimmed);
    QSaveFile out(dataDir + QStringLiteral("/samsung.json"));
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    if (!out.commit())
        return false;
    QFile::setPermissions(dataDir + QStringLiteral("/samsung.json"), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

bool clearSamsungToken(const QString &dataDir)
{
    if (dataDir.trimmed().isEmpty())
        return false;
    const QString path = dataDir + QStringLiteral("/samsung.json");
    QFile existing(path);
    if (!existing.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument current = QJsonDocument::fromJson(existing.readAll());
    existing.close();
    if (!current.isObject())
        return false;
    QJsonObject obj = current.object();
    obj.remove(QStringLiteral("token"));
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    if (!out.commit())
        return false;
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

QString tvTraceLine(const QString &key, const QString &action, const QString &command, const QString &result)
{
    const auto field = [](const QString &value) {
        const QString trimmed = value.trimmed();
        return trimmed.isEmpty() ? QStringLiteral("—") : trimmed;
    };
    return field(key) + QStringLiteral(" -> ") + field(action) + QStringLiteral(" -> ") + field(command)
           + QStringLiteral(" -> ") + field(result);
}

QByteArray samsungKeyPayload(TvKey key)
{
    QJsonObject params;
    params.insert(QStringLiteral("Cmd"), QStringLiteral("Click"));
    params.insert(QStringLiteral("DataOfCmd"), QString::fromLatin1(tvKeyCode(key)));
    params.insert(QStringLiteral("Option"), QStringLiteral("false"));
    params.insert(QStringLiteral("TypeOfRemote"), QStringLiteral("SendRemoteKey"));
    QJsonObject root;
    root.insert(QStringLiteral("method"), QStringLiteral("ms.remote.control"));
    root.insert(QStringLiteral("params"), params);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QString samsungUserMessage(const QString &error, const SamsungConfig &config)
{
    return QStringLiteral("TV Samsung (%1:%2): %3 Volume local até a TV responder.")
        .arg(config.host, QString::number(config.port), error);
}

TvPower parsePowerState(const QString &raw)
{
    const QString state = raw.trimmed().toLower();
    if (state == QLatin1String("on"))
        return TvPower::On;
    if (state == QLatin1String("standby") || state == QLatin1String("ready") || state == QLatin1String("sleep"))
        return TvPower::Standby;
    return TvPower::Unknown;
}

TvProbe parseProbeJson(const QByteArray &body)
{
    TvProbe probe;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return probe;
    const QJsonObject root = doc.object();
    const QJsonObject device = root.value(QStringLiteral("device")).toObject();
    probe.model = device.value(QStringLiteral("modelName")).toString().trimmed();
    if (probe.model.isEmpty())
        probe.model = device.value(QStringLiteral("model")).toString().trimmed();
    probe.name = root.value(QStringLiteral("name")).toString().trimmed();
    if (probe.name.isEmpty())
        probe.name = device.value(QStringLiteral("name")).toString().trimmed();
    const QJsonValue power = powerValue(root, device);
    if (power.isString())
        probe.power = parsePowerState(power.toString());
    return probe;
}

int parseUpnpVolume(const QByteArray &body)
{
    const QRegularExpression re(QStringLiteral("CurrentVolume[^0-9]{0,40}(\\d{1,3})"));
    const QRegularExpressionMatch match = re.match(QString::fromUtf8(body));
    if (!match.hasMatch())
        return -1;
    const int value = match.captured(1).toInt();
    if (value < 0 || value > 100)
        return -1;
    return value;
}

int parseUpnpMute(const QByteArray &body)
{
    const QRegularExpression re(QStringLiteral("CurrentMute[^A-Za-z0-9]{0,40}(true|false|0|1)"),
                                QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(QString::fromUtf8(body));
    if (!match.hasMatch())
        return -1;
    const QString value = match.captured(1).toLower();
    if (value == QLatin1String("1") || value == QLatin1String("true"))
        return 1;
    if (value == QLatin1String("0") || value == QLatin1String("false"))
        return 0;
    return -1;
}

bool parseMac(const QString &raw, quint8 out[6])
{
    QString hex;
    hex.reserve(raw.size());
    for (const QChar ch : raw) {
        if (ch.isDigit() || (ch >= QLatin1Char('a') && ch <= QLatin1Char('f'))
            || (ch >= QLatin1Char('A') && ch <= QLatin1Char('F')))
            hex.append(ch);
    }
    if (hex.size() != 12)
        return false;
    for (int i = 0; i < 6; ++i) {
        bool ok = false;
        const uint value = hex.mid(i * 2, 2).toUInt(&ok, 16);
        if (!ok)
            return false;
        out[i] = quint8(value);
    }
    return true;
}

QByteArray wolMagicPacket(const quint8 mac[6])
{
    QByteArray packet(102, '\0');
    for (int i = 0; i < 6; ++i)
        packet[i] = char(0xFF);
    for (int copy = 0; copy < 16; ++copy) {
        for (int i = 0; i < 6; ++i)
            packet[6 + copy * 6 + i] = char(mac[i]);
    }
    return packet;
}

QString cecDevicePath(const SamsungConfig &config)
{
    const QString value = config.cec.trimmed();
    const QString lower = value.toLower();
    if (lower == QLatin1String("off") || lower == QLatin1String("0") || lower == QLatin1String("false")
        || lower == QLatin1String("no"))
        return {};
    if (!value.isEmpty())
        return value;
    return QStringLiteral("/dev/cec0");
}

bool cecReady(const QString &path)
{
    return !path.isEmpty() && QFileInfo::exists(path);
}

QStringList cecWakeArgs(const QString &device)
{
    return {QStringLiteral("-d"), device, QStringLiteral("--playback"), QStringLiteral("--to"), QStringLiteral("0"),
            QStringLiteral("--image-view-on")};
}

PowerPlan planPowerToggle(TvPower power, bool reachable, bool hasMac, bool cecAvailable)
{
    PowerPlan plan;
    if (reachable && power == TvPower::On) {
        plan.deviceConnect = true;
        plan.turnOff = true;
        return plan;
    }
    if (reachable && power == TvPower::Unknown) {
        plan.deviceConnect = true;
        return plan;
    }
    plan.deviceConnect = true;
    plan.wakeOnLan = hasMac;
    plan.cec = cecAvailable;
    return plan;
}

bool sendWakeOnLan(const quint8 mac[6], const QString &host, QString *error)
{
#ifndef Q_OS_UNIX
    Q_UNUSED(mac);
    Q_UNUSED(host);
    if (error)
        *error = QStringLiteral("Wake-on-LAN só no Linux");
    return false;
#else
    const QByteArray packet = wolMagicPacket(mac);
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        if (error)
            *error = QStringLiteral("sem soquete UDP");
        return false;
    }
    const int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    bool sent = false;
    const auto poke = [&](quint32 address, quint16 port) {
        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_port = htons(port);
        dest.sin_addr.s_addr = htonl(address);
        if (::sendto(fd, packet.constData(), packet.size(), 0, reinterpret_cast<sockaddr *>(&dest), sizeof(dest))
            == packet.size())
            sent = true;
    };
    poke(0xFFFFFFFFu, 9);
    poke(0xFFFFFFFFu, 7);
    const QHostAddress ip(host);
    if (ip.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 raw = ip.toIPv4Address();
        poke(raw, 9);
        poke((raw & 0xFFFFFF00u) | 0xFFu, 9);
    }
    ::close(fd);
    if (!sent && error)
        *error = QStringLiteral("pacote não saiu");
    return sent;
#endif
}

SamsungClient::SamsungClient(QObject *parent)
    : QObject(parent)
{
}

QString SamsungClient::powerLabel() const
{
    if (m_controlOff)
        return QStringLiteral("controle desligado");
    if (!m_probed)
        return QStringLiteral("—");
    if (!m_reachable)
        return QStringLiteral("sem resposta");
    switch (m_power) {
    case TvPower::On:
        return QStringLiteral("ligada");
    case TvPower::Standby:
        return QStringLiteral("desligada");
    case TvPower::Unknown:
        break;
    }
    return QStringLiteral("desconhecido");
}

QString SamsungClient::volumeLabel() const
{
    if (m_volume < 0)
        return m_probed ? QStringLiteral("indisponível") : QStringLiteral("—");
    return QString::number(m_volume);
}

QString SamsungClient::muteLabel() const
{
    if (m_muted < 0)
        return QStringLiteral("—");
    return m_muted > 0 ? QStringLiteral("mudo") : QStringLiteral("com som");
}

bool SamsungClient::tokenPresent() const
{
    return !loadSamsungConfig(m_dataDir).token.trimmed().isEmpty();
}

int SamsungClient::latencyMs() const
{
    if (m_activeStarted <= 0)
        return 0;
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_activeStarted;
    return elapsed < 0 ? 0 : int(elapsed);
}

QString SamsungClient::statusLine() const
{
    return QStringLiteral("%1 · volume %2").arg(powerLabel(), volumeLabel());
}

void SamsungClient::send(TvKey key)
{
    sendKey(key, false);
}

void SamsungClient::sendKey(TvKey key, bool quietPowerError, qint64 started)
{
    if (started <= 0)
        started = QDateTime::currentMSecsSinceEpoch();
    const SamsungConfig config = loadSamsungConfig(m_dataDir);
    if (config.disabled) {
        const QString message = QStringLiteral("Controle Samsung desligado.");
        m_lastError = message;
        m_scheme = QStringLiteral("—");
        emit statusChanged();
        emit commandFinished(int(key), false, message, 0);
        emit failed(int(key), message);
        return;
    }
    if (m_busy) {
        m_queued.key = key;
        m_queued.quietPowerError = quietPowerError;
        m_queued.started = started;
        m_hasQueued = true;
        return;
    }
    m_busy = true;
    m_quietPowerError = quietPowerError;
    m_activeStarted = started;
    const bool secureFirst = config.token.trimmed().isEmpty();
    sendOn(key, secureFirst);
}

void SamsungClient::finishSocket(const QString &error, SocketFollow follow, TvKey key, bool quiet)
{
    if (!m_busy && !m_pairing)
        return;
    if (follow == SocketFollow::TrySecure && !error.isEmpty()) {
        m_lastError = error;
        m_scheme = QStringLiteral("wss");
        emit statusChanged();
        std::fprintf(stderr, "zappe: samsung: porta simples recusou; abrindo wss para o Allow\n");
        sendOn(key, true);
        return;
    }
    if (follow == SocketFollow::RetryBare && !error.isEmpty()) {
        m_lastError = error;
        m_scheme = QStringLiteral("wss");
        emit statusChanged();
        m_stripToken = true;
        std::fprintf(stderr, "zappe: samsung: token recusado; abrindo de novo sem token\n");
        sendOn(key, true);
        return;
    }
    const int waited = latencyMs();
    const bool repairing = m_repairing;
    m_busy = false;
    m_pairing = false;
    m_stripToken = false;
    m_repairing = false;
    if (!error.isEmpty()) {
        m_lastError = error;
        if (quiet)
            std::fprintf(stderr, "zappe: samsung: pareamento não concluiu (%s)\n", qPrintable(error));
        else if (!(key == TvKey::Power && m_quietPowerError))
            emit failed(int(key), error);
        else
            std::fprintf(stderr, "zappe: KEY_POWER não confirmou (%s); Wake-on-LAN/CEC seguem valendo\n",
                         qPrintable(error));
        if (repairing)
            emit repairFinished(false, error, waited);
        else if (!quiet)
            emit commandFinished(int(key), false, error, waited);
    } else {
        m_lastError.clear();
        if (quiet)
            std::fprintf(stderr, "zappe: samsung: pareado\n");
        else {
            emit sent(int(key));
            if (key == TvKey::VolumeUp || key == TvKey::VolumeDown || key == TvKey::Mute || key == TvKey::Power)
                QTimer::singleShot(350, this, [this] { refresh(); });
        }
        if (repairing)
            emit repairFinished(true, QStringLiteral("ok"), waited);
        else if (!quiet)
            emit commandFinished(int(key), true, QStringLiteral("ok"), waited);
    }
    emit statusChanged();
    if (m_hasQueued) {
        const Pending next = m_queued;
        m_hasQueued = false;
        sendKey(next.key, next.quietPowerError, next.started);
        return;
    }
    if (m_repairPending)
        beginRepair();
}

void SamsungClient::setDataDir(const QString &dataDir)
{
    m_dataDir = dataDir;
    const SamsungConfig config = loadSamsungConfig(m_dataDir);
    m_scheme = config.disabled ? QStringLiteral("—")
                               : (config.token.trimmed().isEmpty() ? QStringLiteral("wss") : QStringLiteral("ws"));
    emit statusChanged();
    ensurePaired();
}

void SamsungClient::ensurePaired()
{
    const SamsungConfig config = loadSamsungConfig(m_dataDir);
    if (config.disabled || !config.token.trimmed().isEmpty())
        return;
    if (m_busy || m_pairing)
        return;
    m_busy = true;
    m_pairing = true;
    m_activeStarted = QDateTime::currentMSecsSinceEpoch();
    std::fprintf(stderr, "zappe: samsung: sem token; o pareamento seguro vai pedir Allow na TV\n");
    openSocket(TvKey::VolumeUp, true, true);
}

void SamsungClient::repair()
{
    qunsetenv("ZAPPE_SAMSUNG_TOKEN");
    clearSamsungToken(m_dataDir);
    m_repairStarted = QDateTime::currentMSecsSinceEpoch();
    m_scheme = QStringLiteral("wss");
    m_lastError.clear();
    emit statusChanged();
    if (m_busy) {
        m_repairPending = true;
        return;
    }
    beginRepair();
}

void SamsungClient::beginRepair()
{
    m_repairPending = false;
    m_repairing = true;
    m_pairing = true;
    m_busy = true;
    m_stripToken = true;
    m_activeStarted = m_repairStarted > 0 ? m_repairStarted : QDateTime::currentMSecsSinceEpoch();
    std::fprintf(stderr, "zappe: samsung: sem token; o pareamento seguro vai pedir Allow na TV\n");
    openSocket(TvKey::VolumeUp, true, true);
}

void SamsungClient::sendOn(TvKey key, bool secure)
{
    openSocket(key, secure, m_pairing);
}

void SamsungClient::openSocket(TvKey key, bool secure, bool pairOnly)
{
    SamsungConfig config = loadSamsungConfig(m_dataDir);
    const bool hadToken = !config.token.trimmed().isEmpty();
    if (m_stripToken)
        config.token.clear();
    const bool tokenPresent = !config.token.trimmed().isEmpty();
    const int waitMs = (secure && !tokenPresent) ? samsungPairingWaitMs() : 1800;
    m_scheme = secure ? QStringLiteral("wss") : QStringLiteral("ws");
    emit statusChanged();
    std::fprintf(stderr, "zappe: samsung: abrindo %s://%s:%d token=%s secure=%s espera=%d\n", secure ? "wss" : "ws",
                 qPrintable(config.host), secure ? config.securePort : config.port, tokenPresent ? "sim" : "não",
                 secure ? "sim" : "não", waitMs);
    auto *socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    const auto once = std::make_shared<bool>(true);
    const auto done = [this, socket, timer, once](const QString &error, SocketFollow follow, TvKey sentKey, bool quiet) {
        if (!*once)
            return;
        *once = false;
        timer->stop();
        socket->close();
        socket->deleteLater();
        timer->deleteLater();
        finishSocket(error, follow, sentKey, quiet);
    };
    connect(socket, &QWebSocket::sslErrors, this, [socket](const QList<QSslError> &) {
        std::fprintf(stderr, "zappe: samsung: certificado aceito\n");
        socket->ignoreSslErrors();
    });
    connect(timer, &QTimer::timeout, this, [=] {
        std::fprintf(stderr, "zappe: samsung: tempo esgotado secure=%s\n", secure ? "sim" : "não");
        const SocketFollow follow = secure ? SocketFollow::Finish : SocketFollow::TrySecure;
        done(QStringLiteral("aceite o Zappe na TV"), follow, key, pairOnly);
    });
    connect(socket, &QWebSocket::textMessageReceived, this, [=](const QString &message) {
        const QByteArray raw = message.toUtf8();
        const QJsonDocument doc = QJsonDocument::fromJson(raw);
        const QString event = doc.object().value(QStringLiteral("event")).toString();
        if (!event.isEmpty())
            std::fprintf(stderr, "zappe: samsung: evento %s\n", qPrintable(event));
        if (samsungAuthDenied(raw)) {
            if (samsungUpgradeToSecure(secure, true)) {
                done(QStringLiteral("aceite o Zappe na TV"), SocketFollow::TrySecure, key, false);
                return;
            }
            if (samsungRetryWithoutToken(secure, hadToken, m_stripToken, true)) {
                done(QStringLiteral("aceite o Zappe na TV"), SocketFollow::RetryBare, key, false);
                return;
            }
            if (samsungShouldHoldPairing(secure, tokenPresent, true)) {
                std::fprintf(stderr, "zappe: samsung: aguardando Allow na TV; o socket continua aberto\n");
                return;
            }
            done(QStringLiteral("aceite o Zappe na TV"), SocketFollow::Finish, key, pairOnly);
            return;
        }
        if (event != QLatin1String("ms.channel.connect"))
            return;
        const QString token = samsungConnectToken(raw);
        if (!token.isEmpty()) {
            if (saveSamsungToken(m_dataDir, config.host, config.name, token))
                std::fprintf(stderr, "zappe: samsung: token gravado em %s/samsung.json\n", qPrintable(m_dataDir));
            else
                std::fprintf(stderr, "zappe: samsung: conectado, token não gravado\n");
        } else {
            std::fprintf(stderr, "zappe: samsung: conectado sem token novo\n");
        }
        const bool sendKeyNow = !pairOnly || m_hasQueued;
        const TvKey wire = m_hasQueued ? m_queued.key : key;
        if (sendKeyNow) {
            socket->sendTextMessage(QString::fromUtf8(samsungKeyPayload(wire)));
            std::fprintf(stderr, "zappe: samsung: tecla %s enviada\n", tvKeyCode(wire));
            m_hasQueued = false;
        }
        QTimer::singleShot(180, this, [=] { done({}, SocketFollow::Finish, wire, !sendKeyNow); });
    });
    connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this,
            [=](QAbstractSocket::SocketError) {
                std::fprintf(stderr, "zappe: samsung: erro de socket (%s)\n", qPrintable(socket->errorString()));
                const SocketFollow follow = secure ? SocketFollow::Finish : SocketFollow::TrySecure;
                done(socket->errorString(), follow, key, pairOnly);
            });
    timer->start(waitMs);
    socket->open(QUrl(samsungWebsocketUrl(config, secure)));
}

void SamsungClient::togglePower()
{
    beginProbe(true);
}

void SamsungClient::refresh()
{
    beginProbe(false);
    ensurePaired();
}

void SamsungClient::nudgeHdmi(int delta)
{
    const int next = qBound(1, m_hdmi + delta, 3);
    if (next == m_hdmi)
        return;
    m_hdmi = next;
    emit statusChanged();
}

void SamsungClient::sendHdmi()
{
    TvKey key = TvKey::Hdmi1;
    if (m_hdmi == 2)
        key = TvKey::Hdmi2;
    else if (m_hdmi >= 3)
        key = TvKey::Hdmi3;
    emit reported(QStringLiteral("Entrada HDMI %1").arg(m_hdmi));
    send(key);
}

void SamsungClient::beginProbe(bool toggle)
{
    const SamsungConfig config = loadSamsungConfig(m_dataDir);
    if (config.disabled) {
        m_probed = true;
        m_reachable = false;
        m_controlOff = true;
        m_power = TvPower::Unknown;
        m_volume = -1;
        m_muted = -1;
        m_scheme = QStringLiteral("—");
        emit statusChanged();
        if (toggle)
            emit failed(int(TvKey::Power), QStringLiteral("Controle Samsung desligado."));
        return;
    }
    m_controlOff = false;
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    const int generation = ++m_probeGen;
    QNetworkRequest request(QUrl(QStringLiteral("http://%1:%2/api/v2/").arg(config.host).arg(config.port)));
    request.setTransferTimeout(800);
    QNetworkReply *reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, toggle] {
        const bool current = generation == m_probeGen;
        const bool httpOk = reply->error() == QNetworkReply::NoError;
        const QByteArray body = reply->isReadable() ? reply->readAll() : QByteArray();
        reply->deleteLater();
        if (!current)
            return;
        applyProbe(httpOk, httpOk ? parseProbeJson(body) : TvProbe{}, toggle);
    });
}

void SamsungClient::applyProbe(bool httpOk, const TvProbe &probe, bool toggle)
{
    m_probed = true;
    m_reachable = httpOk;
    m_power = httpOk ? probe.power : TvPower::Unknown;
    if (!httpOk || m_power != TvPower::On) {
        m_volume = -1;
        m_muted = -1;
    }
    emit statusChanged();
    if (httpOk && m_power == TvPower::On) {
        queryVolume();
        queryMute();
    }
    if (!toggle)
        return;

    const SamsungConfig config = loadSamsungConfig(m_dataDir);
    quint8 mac[6] = {};
    const bool hasMac = parseMac(config.mac, mac);
    const QString cecPath = cecDevicePath(config);
    const PowerPlan plan = planPowerToggle(m_power, httpOk, hasMac, cecReady(cecPath));
    QStringList how;
    if (plan.deviceConnect)
        how.append(QStringLiteral("Device Connect"));
    if (plan.wakeOnLan)
        how.append(QStringLiteral("Wake-on-LAN"));
    if (plan.cec)
        how.append(QStringLiteral("HDMI-CEC"));
    QString text = plan.turnOff ? QStringLiteral("Desligando a TV") : QStringLiteral("Ligando a TV");
    if (!how.isEmpty())
        text += QStringLiteral(" · ") + how.join(QStringLiteral(", "));
    if (plan.wakeOnLan) {
        QString error;
        if (!sendWakeOnLan(mac, config.host, &error))
            text += QStringLiteral(" (Wake-on-LAN falhou)");
    }
    if (plan.cec && !QProcess::startDetached(QStringLiteral("cec-ctl"), cecWakeArgs(cecPath)))
        text += QStringLiteral(" (HDMI-CEC indisponível)");
    emit reported(text);
    if (plan.deviceConnect)
        sendKey(TvKey::Power, !plan.turnOff);
}

void SamsungClient::queryVolume()
{
    const SamsungConfig config = loadSamsungConfig(m_dataDir);
    if (config.disabled)
        return;
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl(QStringLiteral("http://%1:9197/upnp/control/RenderingControl1").arg(config.host)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/xml; charset=\"utf-8\""));
    request.setRawHeader("SOAPACTION", "\"urn:schemas-upnp-org:service:RenderingControl:1#GetVolume\"");
    request.setTransferTimeout(800);
    const QByteArray soap = QByteArrayLiteral(
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
        "<u:GetVolume xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">"
        "<InstanceID>0</InstanceID><Channel>Master</Channel></u:GetVolume>"
        "</s:Body></s:Envelope>");
    QNetworkReply *reply = m_net->post(request, soap);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const bool ok = reply->error() == QNetworkReply::NoError;
        const QByteArray body = reply->isReadable() ? reply->readAll() : QByteArray();
        reply->deleteLater();
        const int volume = ok ? parseUpnpVolume(body) : -1;
        if (volume == m_volume)
            return;
        m_volume = volume;
        emit statusChanged();
    });
}

void SamsungClient::queryMute()
{
    const SamsungConfig config = loadSamsungConfig(m_dataDir);
    if (config.disabled)
        return;
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl(QStringLiteral("http://%1:9197/upnp/control/RenderingControl1").arg(config.host)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/xml; charset=\"utf-8\""));
    request.setRawHeader("SOAPACTION", "\"urn:schemas-upnp-org:service:RenderingControl:1#GetMute\"");
    request.setTransferTimeout(800);
    const QByteArray soap = QByteArrayLiteral(
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
        "<u:GetMute xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">"
        "<InstanceID>0</InstanceID><Channel>Master</Channel></u:GetMute>"
        "</s:Body></s:Envelope>");
    QNetworkReply *reply = m_net->post(request, soap);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const bool ok = reply->error() == QNetworkReply::NoError;
        const QByteArray body = reply->isReadable() ? reply->readAll() : QByteArray();
        reply->deleteLater();
        const int muted = ok ? parseUpnpMute(body) : -1;
        if (muted == m_muted)
            return;
        m_muted = muted;
        emit statusChanged();
    });
}

} // namespace zappe
