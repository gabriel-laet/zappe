#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;

namespace zappe {

// Samsung UN43TU7020 na rede local, pelo websocket do Device Connect
// (o mesmo canal que o controle da TV usa; na sala isso é o
// SmartThings/Device Connect, não a nuvem). Configuração só em
// ~/.local/share/zappe/samsung.json e nas variáveis ZAPPE_SAMSUNG_*.

struct SamsungConfig {
    QString host = QStringLiteral("192.168.3.6");
    int port = 8001;
    int securePort = 8002;
    QString token;
    QString name = QStringLiteral("Zappe");
    QString mac;
    // Vazio = tentar /dev/cec0. "off" desliga o HDMI-CEC.
    QString cec;
    bool disabled = false;
};

enum class TvKey { VolumeUp, VolumeDown, Mute, Power, Hdmi1, Hdmi2, Hdmi3, Hdmi4 };

enum class TvPower { Unknown, On, Standby };

struct TvProbe {
    TvPower power = TvPower::Unknown;
    QString model;
    QString name;
};

// Ligada → só KEY_POWER (desligar). Standby ou sem resposta → ligar
// por Device Connect e, se existirem, Wake-on-LAN e HDMI-CEC.
struct PowerPlan {
    bool deviceConnect = false;
    bool wakeOnLan = false;
    bool cec = false;
    bool turnOff = false;
};

SamsungConfig loadSamsungConfig(const QString &dataDir);
QString samsungWebsocketUrl(const SamsungConfig &config, bool secure);
const char *tvKeyCode(TvKey key);
QByteArray samsungKeyPayload(TvKey key);
QString samsungUserMessage(const QString &error, const SamsungConfig &config);

TvPower parsePowerState(const QString &raw);
TvProbe parseProbeJson(const QByteArray &body);
int parseUpnpVolume(const QByteArray &body);
bool parseMac(const QString &raw, quint8 out[6]);
QByteArray wolMagicPacket(const quint8 mac[6]);
QString cecDevicePath(const SamsungConfig &config);
bool cecReady(const QString &path);
QStringList cecWakeArgs(const QString &device);
PowerPlan planPowerToggle(TvPower power, bool reachable, bool hasMac, bool cecAvailable);
bool sendWakeOnLan(const quint8 mac[6], const QString &host, QString *error);

class SamsungClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString powerLabel READ powerLabel NOTIFY statusChanged)
    Q_PROPERTY(QString volumeLabel READ volumeLabel NOTIFY statusChanged)
    Q_PROPERTY(int hdmiPort READ hdmiPort NOTIFY statusChanged)
    Q_PROPERTY(QString statusLine READ statusLine NOTIFY statusChanged)

public:
    explicit SamsungClient(QObject *parent = nullptr);
    void setDataDir(const QString &dataDir) { m_dataDir = dataDir; }

    QString powerLabel() const;
    QString volumeLabel() const;
    int hdmiPort() const { return m_hdmi; }
    QString statusLine() const;

    void send(TvKey key);
    Q_INVOKABLE void togglePower();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void nudgeHdmi(int delta);
    Q_INVOKABLE void sendHdmi();

signals:
    void failed(int key, const QString &message);
    void sent(int key);
    void statusChanged();
    void reported(const QString &message);

private:
    void sendKey(TvKey key, bool quietPowerError);
    void sendOn(TvKey key, bool secure);
    void beginProbe(bool toggle);
    void applyProbe(bool httpOk, const TvProbe &probe, bool toggle);
    void queryVolume();
    void finishSocket(const QString &error, bool trySecure, TvKey key);

    struct Pending {
        TvKey key = TvKey::VolumeUp;
        bool quietPowerError = false;
    };

    QString m_dataDir;
    bool m_busy = false;
    bool m_quietPowerError = false;
    bool m_hasQueued = false;
    Pending m_queued;
    bool m_probed = false;
    bool m_reachable = false;
    bool m_controlOff = false;
    TvPower m_power = TvPower::Unknown;
    int m_volume = -1;
    int m_hdmi = 1;
    int m_probeGen = 0;
    QNetworkAccessManager *m_net = nullptr;
};

} // namespace zappe
