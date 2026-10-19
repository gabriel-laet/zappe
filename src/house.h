#pragma once

#include "keymap.h"
#include "samsung.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QProcess;
class QQuickWindow;
class QTimer;

namespace zappe {

class Guide;

// Liga o guia ao Chrome (CDP), à TV aberta, ao controle, à Samsung e à voz.
class House : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(bool promptOpen READ promptOpen NOTIFY changed)
    Q_PROPERTY(QString promptService READ promptService NOTIFY changed)
    Q_PROPERTY(QString promptField READ promptField NOTIFY changed)
    Q_PROPERTY(QString promptText READ promptText NOTIFY changed)
    Q_PROPERTY(bool promptSecret READ promptSecret NOTIFY changed)
    Q_PROPERTY(QString draft READ draft WRITE setDraft NOTIFY changed)
    Q_PROPERTY(QString promptHint READ promptHint NOTIFY changed)
    Q_PROPERTY(bool listening READ listening NOTIFY changed)
    Q_PROPERTY(QString listeningText READ listeningText NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool loadingFailed READ loadingFailed NOTIFY changed)
    Q_PROPERTY(QString loadingTitle READ loadingTitle NOTIFY changed)
    Q_PROPERTY(QString loadingArt READ loadingArt NOTIFY changed)
    Q_PROPERTY(QString loadingStatus READ loadingStatus NOTIFY changed)
    Q_PROPERTY(QString loadingError READ loadingError NOTIFY changed)
    Q_PROPERTY(qreal loadingProgress READ loadingProgress NOTIFY changed)
    Q_PROPERTY(QStringList tvLog READ tvLog NOTIFY tvLogChanged)

public:
    explicit House(QObject *parent = nullptr);

    void start(Guide *guide, QQuickWindow *window, const QString &dataDir);

    bool playing() const { return m_playing; }
    bool promptOpen() const { return m_promptOpen; }
    QString promptService() const { return m_promptService; }
    QString promptField() const { return m_promptField; }
    QString promptText() const { return m_promptText; }
    bool promptSecret() const { return m_promptSecret; }
    QString draft() const { return m_draft; }
    void setDraft(const QString &draft);
    QString promptHint() const { return m_promptHint; }
    bool listening() const { return m_listening; }
    QString listeningText() const { return m_listeningText; }
    bool loading() const { return m_loading; }
    bool loadingFailed() const { return m_loadingFailed; }
    QString loadingTitle() const { return m_loadingTitle; }
    QString loadingArt() const { return m_loadingArt; }
    QString loadingStatus() const { return m_loadingStatus; }
    QString loadingError() const { return m_loadingError; }
    qreal loadingProgress() const { return m_loadingProgress; }
    QStringList tvLog() const { return m_tvLog; }

    Q_INVOKABLE void submitPrompt(const QString &value);
    Q_INVOKABLE void cancelPrompt();
    Q_INVOKABLE void stopPlayback();
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goHome();
    Q_INVOKABLE bool acceptBandScan();
    Q_INVOKABLE void cancelLoading();
    Q_INVOKABLE void retryLoading();
    Q_INVOKABLE void dial(const QString &glyph);
    Q_INVOKABLE void noteX11Key(int scan, int qtKey);
    QObject *tv() const;

    static constexpr int loadingTimeoutMs = 10000;

public slots:
    void playRequested(const QString &title, const QString &service, const QString &href);
    void onRemote(int remoteAction);
    void onDial(int value);
    void onEvdevKey(int code);

signals:
    void changed();
    void tvLogChanged();

private:
    void openService(const QString &id);
    void openUrl(const QString &url);
    void playOta(const QString &query);
    void setPlaying(bool playing);
    void showGuide();
    void hideGuide();
    void toast(const QString &text);
    void handleVoice(const QString &transcript);
    void volume(int delta);
    void mute();

    struct TvTrace {
        QString key;
        QString action;
        QString command;
        QString result;
        int waitKey = -1;
        qint64 at = 0;
        bool x11 = false;
        bool evdev = false;
        bool loggedFinal = false;
        QString logged;
    };

    void watchCommand(TvKey key);
    void runTvTest(int index);
    void noteRemoteAction(RemoteAction action);
    void finishWait(int waitKey, const QString &result);
    void onCommandFinished(int key, bool ok, const QString &detail, int latencyMs);
    void onRepairFinished(bool ok, const QString &detail, int latencyMs);
    void onTvReported(const QString &message);
    void logLink();
    void syncTvTrace();
    bool tracing() const;
    void rememberTrace(const TvTrace &line);
    void publishTrace(int index);
    QString formatTrace(const TvTrace &line) const;
    bool take(RemoteAction action);
    void onSettings(int index, int verb);
    void beginLoading(const QString &title, const QString &art, const QString &kind);
    void tickLoading();
    void finishLoading();
    void failLoading(const QString &message);
    void onHistory(bool wentBack);
    void chromeBack();
    void leavePlayer();
    void showMirror(const QString &serviceId);
    void updateKeyHold();
    bool chromeFocused() const;
    void forwardChromeKey(RemoteAction action);
    void commitDial();
    void refreshFocusedArt();
    void applyEpg(const QString &channel, const QString &nowTitle, const QString &nextTitle);
    void startBandScan();
    void finishBandScan();

    Guide *m_guide = nullptr;
    QQuickWindow *m_window = nullptr;
    QString m_dataDir;
    class ChromeSession *m_chrome = nullptr;
    class LoginRunner *m_login = nullptr;
    class CdpClient *m_direct = nullptr;
    class OtaPlayer *m_ota = nullptr;
    class SamsungClient *m_samsung = nullptr;
    class VoiceSession *m_voice = nullptr;
    class RemoteWatcher *m_remote = nullptr;
    class MirrorSession *m_mirror = nullptr;
    class XBackGrab *m_xback = nullptr;
    bool m_playing = false;
    bool m_player = false;
    bool m_promptOpen = false;
    bool m_promptSecret = false;
    bool m_listening = false;
    bool m_directPending = false;
    QString m_promptService;
    QString m_promptField;
    QString m_promptText;
    QString m_draft;
    QString m_promptHint;
    QString m_listeningText;
    QString m_pendingUrl;
    qint64 m_lastRemote = 0;
    int m_lastAction = 0;
    bool m_loading = false;
    bool m_loadingFailed = false;
    QString m_loadingTitle;
    QString m_loadingArt;
    QString m_loadingStatus;
    QString m_loadingError;
    QString m_loadingKind;
    qreal m_loadingProgress = 0;
    qint64 m_loadStarted = 0;
    QString m_retryTitle;
    QString m_retryService;
    QString m_retryHref;
    QString m_chromeOwner;
    class QTimer *m_loadTimer = nullptr;
    class QTimer *m_dialTimer = nullptr;
    class QTimer *m_focusTimer = nullptr;
    qint64 m_lastBack = 0;
    QString m_refreshTitle;
    qint64 m_refreshAt = 0;
    bool m_bandOffer = false;
    qint64 m_bandConsumed = 0;
    QProcess *m_bandScan = nullptr;
    QVector<TvTrace> m_traces;
    QStringList m_tvLog;
    QString m_linkLogged;
    qint64 m_traceUntil = 0;
    class QTimer *m_tvTimer = nullptr;
};

} // namespace zappe
