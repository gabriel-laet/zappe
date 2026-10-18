#include "samsung.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <QWebSocket>

#include <cstdio>

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

const char *keyCode(TvKey key)
{
    switch (key) {
    case TvKey::VolumeUp:
        return "KEY_VOLUP";
    case TvKey::VolumeDown:
        return "KEY_VOLDOWN";
    case TvKey::Mute:
        return "KEY_MUTE";
    }
    return "KEY_VOLUP";
}

} // namespace

SamsungConfig loadSamsungConfig(const QString &dataDir)
{
    SamsungConfig config;
    config.host = envOr("ZAPPE_SAMSUNG_HOST", config.host);
    config.name = envOr("ZAPPE_SAMSUNG_NAME", config.name);
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

QByteArray samsungKeyPayload(TvKey key)
{
    QJsonObject params;
    params.insert(QStringLiteral("Cmd"), QStringLiteral("Click"));
    params.insert(QStringLiteral("DataOfCmd"), QString::fromLatin1(keyCode(key)));
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

SamsungClient::SamsungClient(QObject *parent)
    : QObject(parent)
{
}

void SamsungClient::send(TvKey key)
{
    if (m_busy)
        return;
    const SamsungConfig config = loadSamsungConfig(m_dataDir);
    if (config.disabled) {
        emit failed(QStringLiteral("Controle Samsung desligado."));
        return;
    }
    m_busy = true;
    sendOn(key, false);
}

void SamsungClient::sendOn(TvKey key, bool secure)
{
    const SamsungConfig config = loadSamsungConfig(m_dataDir);
    auto *socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    const auto finish = [this, socket, timer](const QString &error, bool trySecure, TvKey key) {
        if (!m_busy)
            return;
        timer->stop();
        socket->close();
        socket->deleteLater();
        timer->deleteLater();
        if (trySecure && !error.isEmpty()) {
            sendOn(key, true);
            return;
        }
        m_busy = false;
        if (!error.isEmpty())
            emit failed(error);
        else
            emit sent(key);
    };
    connect(timer, &QTimer::timeout, this, [=] { finish(QStringLiteral("tempo esgotado"), !secure, key); });
    connect(socket, &QWebSocket::textMessageReceived, this, [=](const QString &message) {
        const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
        const QString event = doc.object().value(QStringLiteral("event")).toString();
        if (event == QLatin1String("ms.channel.unauthorized") || event == QLatin1String("ms.channel.timeOut")) {
            finish(QStringLiteral("aceite o Zappe no Device Connect"), false, key);
            return;
        }
        if (event != QLatin1String("ms.channel.connect"))
            return;
        const QJsonValue token = doc.object().value(QStringLiteral("data")).toObject().value(QStringLiteral("token"));
        if (token.isString() && !token.toString().isEmpty() && !m_dataDir.isEmpty()) {
            QJsonObject obj;
            QFile existing(m_dataDir + QStringLiteral("/samsung.json"));
            if (existing.open(QIODevice::ReadOnly)) {
                const QJsonDocument current = QJsonDocument::fromJson(existing.readAll());
                if (current.isObject())
                    obj = current.object();
            }
            if (!obj.contains(QStringLiteral("host")))
                obj.insert(QStringLiteral("host"), config.host);
            obj.insert(QStringLiteral("token"), token.toString());
            if (!obj.contains(QStringLiteral("name")))
                obj.insert(QStringLiteral("name"), config.name);
            QSaveFile out(m_dataDir + QStringLiteral("/samsung.json"));
            if (out.open(QIODevice::WriteOnly)) {
                out.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
                out.commit();
                QFile::setPermissions(m_dataDir + QStringLiteral("/samsung.json"),
                                      QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            }
        }
        socket->sendTextMessage(QString::fromUtf8(samsungKeyPayload(key)));
        QTimer::singleShot(180, this, [=] { finish({}, false, key); });
    });
    connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this,
            [=](QAbstractSocket::SocketError) { finish(socket->errorString(), !secure, key); });
    timer->start(1800);
    socket->open(QUrl(samsungWebsocketUrl(config, secure)));
}

} // namespace zappe
