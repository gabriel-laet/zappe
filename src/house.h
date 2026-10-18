#pragma once

#include "keymap.h"

#include <QObject>
#include <QString>

class QQuickWindow;

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

    Q_INVOKABLE void submitPrompt(const QString &value);
    Q_INVOKABLE void cancelPrompt();
    Q_INVOKABLE void stopPlayback();
    QObject *tv() const;

public slots:
    void playRequested(const QString &title, const QString &service, const QString &href);
    void onRemote(int remoteAction);

signals:
    void changed();

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
    bool take(RemoteAction action);
    void onSettings(int index, int verb);

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
    bool m_playing = false;
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
};

} // namespace zappe
