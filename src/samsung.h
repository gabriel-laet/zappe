#pragma once

#include <QObject>
#include <QString>

namespace zappe {

// Samsung UN43TU7020 via SmartThings Device Connect (websocket).
// Volume e mudo. Power do controle não acorda a TV: a tecla 116 é ignorada.

struct SamsungConfig {
    QString host = QStringLiteral("192.168.3.6");
    int port = 8001;
    int securePort = 8002;
    QString token;
    QString name = QStringLiteral("Zappe");
    bool disabled = false;
};

enum class TvKey { VolumeUp, VolumeDown, Mute };

SamsungConfig loadSamsungConfig(const QString &dataDir);
QString samsungWebsocketUrl(const SamsungConfig &config, bool secure);
QByteArray samsungKeyPayload(TvKey key);
QString samsungUserMessage(const QString &error, const SamsungConfig &config);

class SamsungClient : public QObject {
    Q_OBJECT
public:
    explicit SamsungClient(QObject *parent = nullptr);
    void setDataDir(const QString &dataDir) { m_dataDir = dataDir; }
    void send(TvKey key);

signals:
    void failed(const QString &message);
    void sent(TvKey key);

private:
    void sendOn(TvKey key, bool secure);
    QString m_dataDir;
    bool m_busy = false;
};

} // namespace zappe
