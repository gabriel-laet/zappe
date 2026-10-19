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
QString samsungConnectToken(const QByteArray &message);
bool samsungAuthDenied(const QByteArray &message);
int samsungPairingWaitMs();
bool samsungUpgradeToSecure(bool secure, bool denied);
bool samsungRetryWithoutToken(bool secure, bool hadToken, bool stripped, bool denied);
bool samsungShouldHoldPairing(bool secure, bool tokenPresent, bool denied);
bool saveSamsungToken(const QString &dataDir, const QString &host, const QString &name, const QString &token);
// Tira só a chave token. host, nome e MAC ficam. O valor do token não é impresso.
bool clearSamsungToken(const QString &dataDir);
const char *tvKeyCode(TvKey key);
QByteArray samsungKeyPayload(TvKey key);
QString samsungUserMessage(const QString &error, const SamsungConfig &config);
// Campo vazio vira travessão. Formato: tecla -> ação -> comando -> resposta.
QString tvTraceLine(const QString &key, const QString &action, const QString &command, const QString &result);

TvPower parsePowerState(const QString &raw);
TvProbe parseProbeJson(const QByteArray &body);
int parseUpnpVolume(const QByteArray &body);
// 1 mudo, 0 com som, -1 se o SOAP não trouxer CurrentMute.
int parseUpnpMute(const QByteArray &body);
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
    Q_PROPERTY(QString muteLabel READ muteLabel NOTIFY statusChanged)
    Q_PROPERTY(int hdmiPort READ hdmiPort NOTIFY statusChanged)
    Q_PROPERTY(QString statusLine READ statusLine NOTIFY statusChanged)
    Q_PROPERTY(QString scheme READ scheme NOTIFY statusChanged)
    Q_PROPERTY(bool tokenPresent READ tokenPresent NOTIFY statusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY statusChanged)

public:
    explicit SamsungClient(QObject *parent = nullptr);
    void setDataDir(const QString &dataDir);

    QString powerLabel() const;
    QString volumeLabel() const;
    QString muteLabel() const;
    int hdmiPort() const { return m_hdmi; }
    QString statusLine() const;
    QString scheme() const { return m_scheme; }
    bool tokenPresent() const;
    QString lastError() const { return m_lastError; }

    void send(TvKey key);
    Q_INVOKABLE void togglePower();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void nudgeHdmi(int delta);
    Q_INVOKABLE void sendHdmi();
    // Apaga o token e abre o wss de novo para a TV mostrar Allow.
    void repair();

signals:
    void failed(int key, const QString &message);
    void sent(int key);
    void statusChanged();
    void reported(const QString &message);
    // latencyMs inclui a espera na fila. detail é "ok" ou o erro, sem o token.
    void commandFinished(int key, bool ok, const QString &detail, int latencyMs);
    void repairFinished(bool ok, const QString &detail, int latencyMs);

private:
    void sendKey(TvKey key, bool quietPowerError, qint64 started = 0);
    void sendOn(TvKey key, bool secure);
    void ensurePaired();
    void beginRepair();
    void openSocket(TvKey key, bool secure, bool pairOnly);
    void beginProbe(bool toggle);
    void applyProbe(bool httpOk, const TvProbe &probe, bool toggle);
    void queryVolume();
    void queryMute();
    enum class SocketFollow { Finish, TrySecure, RetryBare };
    void finishSocket(const QString &error, SocketFollow follow, TvKey key, bool quiet);
    int latencyMs() const;

    struct Pending {
        TvKey key = TvKey::VolumeUp;
        bool quietPowerError = false;
        qint64 started = 0;
    };

    QString m_dataDir;
    bool m_busy = false;
    bool m_pairing = false;
    bool m_stripToken = false;
    bool m_quietPowerError = false;
    bool m_hasQueued = false;
    Pending m_queued;
    bool m_probed = false;
    bool m_reachable = false;
    bool m_controlOff = false;
    TvPower m_power = TvPower::Unknown;
    int m_volume = -1;
    int m_muted = -1;
    int m_hdmi = 1;
    QString m_scheme = QStringLiteral("—");
    QString m_lastError;
    bool m_repairPending = false;
    bool m_repairing = false;
    qint64 m_activeStarted = 0;
    qint64 m_repairStarted = 0;
    int m_probeGen = 0;
    QNetworkAccessManager *m_net = nullptr;
};

} // namespace zappe
