#include "house.h"

#include "cdp.h"
#include "chrome.h"
#include "guide.h"
#include "logins.h"
#include "ota.h"
#include "remote.h"
#include "samsung.h"
#include "voice.h"

#include <QDateTime>
#include <QProcess>
#include <QQuickWindow>

namespace zappe {

House::House(QObject *parent)
    : QObject(parent)
    , m_chrome(new ChromeSession(this))
    , m_login(new LoginRunner(this))
    , m_direct(new CdpClient(this))
    , m_ota(new OtaPlayer(this))
    , m_samsung(new SamsungClient(this))
    , m_voice(new VoiceSession(this))
    , m_remote(new RemoteWatcher(this))
{
    connect(m_chrome, &ChromeSession::failed, this, [this](const QString &message) { toast(message); });
    connect(m_chrome, &ChromeSession::ready, this, [this] {
        if (!m_pendingUrl.isEmpty() && m_directPending) {
            m_directPending = false;
            m_direct->attach(m_chrome->port());
        }
    });
    connect(m_direct, &CdpClient::attached, this, [this] {
        if (m_pendingUrl.isEmpty())
            return;
        m_direct->navigate(m_pendingUrl);
    });
    connect(m_direct, &CdpClient::navigated, this, [this] {
        if (m_pendingUrl.isEmpty())
            return;
        setPlaying(true);
        hideGuide();
    });
    connect(m_direct, &CdpClient::failed, this, [this](const QString &message) { toast(message); });
    connect(m_login, &LoginRunner::promptRequested, this,
            [this](const QString &service, const QString &field, const QString &prompt, bool secret) {
                m_promptOpen = true;
                m_promptService = service;
                m_promptField = field;
                m_promptText = prompt;
                m_promptSecret = secret;
                m_draft.clear();
                m_promptHint = webhookEnabled(loadWebhookConfig(m_dataDir))
                                   ? QStringLiteral("Pedido também no webhook.")
                                   : QStringLiteral("Webhook desligado. Digite aqui ou grave login-inbox.json.");
                showGuide();
                emit changed();
            });
    connect(m_login, &LoginRunner::status, this, [this](const QString &message) { toast(message); });
    connect(m_login, &LoginRunner::failed, this, [this](const QString &message) {
        m_promptOpen = false;
        emit changed();
        toast(message);
        showGuide();
    });
    connect(m_login, &LoginRunner::playing, this, [this](const QString &) {
        m_promptOpen = false;
        emit changed();
        setPlaying(true);
        hideGuide();
    });
    connect(m_ota, &OtaPlayer::started, this, [this](const QString &channel) {
        toast(QStringLiteral("TV aberta · ") + channel);
        setPlaying(true);
        hideGuide();
    });
    connect(m_ota, &OtaPlayer::failed, this, [this](const QString &message) { toast(message); });
    connect(m_ota, &OtaPlayer::stopped, this, [this] {
        if (!m_chrome->running())
            setPlaying(false);
    });
    connect(m_samsung, &SamsungClient::failed, this, [this](int key, const QString &message) {
        const auto tvKey = TvKey(key);
        const bool volumeKey = tvKey == TvKey::VolumeUp || tvKey == TvKey::VolumeDown || tvKey == TvKey::Mute;
        if (!volumeKey) {
            toast(QStringLiteral("TV Samsung (%1): %2").arg(loadSamsungConfig(m_dataDir).host, message));
            return;
        }
        toast(samsungUserMessage(message, loadSamsungConfig(m_dataDir)));
        QProcess::startDetached(QStringLiteral("pactl"), {QStringLiteral("set-sink-volume"),
                                                          QStringLiteral("@DEFAULT_SINK@"), QStringLiteral("+0%")});
    });
    connect(m_samsung, &SamsungClient::reported, this, [this](const QString &message) { toast(message); });
    connect(m_voice, &VoiceSession::started, this, [this] {
        m_listening = true;
        m_listeningText = QStringLiteral("Ouvindo…");
        emit changed();
    });
    connect(m_voice, &VoiceSession::failed, this, [this](const QString &message) {
        m_listening = false;
        emit changed();
        toast(message);
    });
    connect(m_voice, &VoiceSession::finished, this, [this](const QString &transcript) {
        m_listening = false;
        m_listeningText = transcript;
        emit changed();
        handleVoice(transcript);
    });
    connect(m_remote, &RemoteWatcher::action, this, &House::onRemote);
}

void House::start(Guide *guide, QQuickWindow *window, const QString &dataDir)
{
    m_guide = guide;
    m_window = window;
    m_dataDir = dataDir;
    m_samsung->setDataDir(dataDir);
    connect(m_guide, &Guide::settingsCommand, this, &House::onSettings);
    connect(m_guide, &Guide::pageChanged, this, [this] {
        if (m_guide && m_guide->page() == QLatin1String("settings"))
            m_samsung->refresh();
    });
    if (m_guide->page() == QLatin1String("settings"))
        m_samsung->refresh();
    m_remote->start();
}

QObject *House::tv() const
{
    return m_samsung;
}

void House::setDraft(const QString &draft)
{
    if (m_draft == draft)
        return;
    m_draft = draft;
    emit changed();
}

void House::submitPrompt(const QString &value)
{
    if (value.trimmed().isEmpty())
        return;
    m_login->submit(value);
    m_promptOpen = false;
    emit changed();
}

void House::cancelPrompt()
{
    m_login->cancel();
    m_promptOpen = false;
    emit changed();
    showGuide();
}

void House::stopPlayback()
{
    m_login->cancel();
    m_promptOpen = false;
    m_pendingUrl.clear();
    m_ota->stop();
    m_chrome->stop();
    setPlaying(false);
    showGuide();
    emit changed();
}

void House::playRequested(const QString &title, const QString &service, const QString &href)
{
    if (service == QLatin1String("ota") || href.startsWith(QStringLiteral("zappe://ota/"))) {
        const QString query = href.startsWith(QStringLiteral("zappe://ota/")) ? href.mid(12) : title;
        playOta(query);
        return;
    }
    const LoginCatalog catalog = loadLoginCatalog();
    if (findService(catalog, service)) {
        openService(service);
        return;
    }
    if (href.startsWith(QStringLiteral("http://")) || href.startsWith(QStringLiteral("https://"))) {
        openUrl(href);
        return;
    }
    toast(QStringLiteral("Sem reprodução para ") + title);
}

void House::openService(const QString &id)
{
    const LoginCatalog catalog = loadLoginCatalog();
    const LoginService *service = findService(catalog, id);
    if (!service) {
        toast(QStringLiteral("Streaming desconhecido."));
        return;
    }
    m_pendingUrl.clear();
    m_directPending = false;
    connect(m_chrome, &ChromeSession::ready, this, [this, service = *service] {
        m_login->start(service, m_chrome->port(), m_dataDir);
    }, Qt::SingleShotConnection);
    m_chrome->ensure(m_dataDir);
}

void House::openUrl(const QString &url)
{
    m_pendingUrl = url;
    m_directPending = true;
    m_chrome->ensure(m_dataDir);
}

void House::playOta(const QString &query)
{
    m_chrome->stop();
    m_ota->play(query);
}

void House::setPlaying(bool playing)
{
    if (m_playing == playing)
        return;
    m_playing = playing;
    emit changed();
}

void House::showGuide()
{
    if (!m_window)
        return;
    m_window->showFullScreen();
    m_window->raise();
    m_window->requestActivate();
}

void House::hideGuide()
{
    if (!m_window)
        return;
    m_window->hide();
}

void House::toast(const QString &text)
{
    if (m_guide)
        m_guide->toast(text);
}

void House::handleVoice(const QString &transcript)
{
    const VoiceIntent intent = parseVoicePtBr(transcript);
    if (!intent.ok()) {
        toast(transcript.isEmpty() ? QStringLiteral("Não entendi.") : transcript);
        return;
    }
    switch (intent.kind) {
    case VoiceKind::Home:
        stopPlayback();
        if (m_guide)
            m_guide->back();
        break;
    case VoiceKind::Back:
        if (m_playing || m_promptOpen)
            stopPlayback();
        else if (m_guide)
            m_guide->back();
        break;
    case VoiceKind::PlayPause:
        if (m_ota->playing())
            m_ota->togglePause();
        else if (m_direct->connected())
            m_direct->evaluate(QStringLiteral(
                "(() => { const ev = new KeyboardEvent('keydown', {key:' ', code:'Space', bubbles:true});"
                " document.dispatchEvent(ev); return 'ok'; })()"));
        break;
    case VoiceKind::Open:
        openService(intent.service);
        break;
    case VoiceKind::Ota:
        playOta(intent.query);
        break;
    case VoiceKind::Volume:
        volume(intent.delta);
        break;
    case VoiceKind::Mute:
        mute();
        break;
    case VoiceKind::None:
        break;
    }
}

void House::volume(int delta)
{
    if (m_guide)
        m_guide->adjustVolume(delta > 0 ? 5 : -5);
    m_samsung->send(delta > 0 ? TvKey::VolumeUp : TvKey::VolumeDown);
}

void House::mute()
{
    if (m_guide)
        m_guide->toggleMute();
    m_samsung->send(TvKey::Mute);
}

bool House::take(RemoteAction action)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastAction == int(action) && now - m_lastRemote < 30)
        return false;
    m_lastAction = int(action);
    m_lastRemote = now;
    return true;
}

void House::onRemote(int remoteAction)
{
    const auto action = RemoteAction(remoteAction);
    if (action == RemoteAction::Ignore || action == RemoteAction::Unmapped)
        return;
    if (!take(action))
        return;
    if (action == RemoteAction::Power) {
        m_samsung->togglePower();
        return;
    }
    if (m_promptOpen) {
        if (action == RemoteAction::Back)
            cancelPrompt();
        else if (action == RemoteAction::Ok)
            submitPrompt(m_draft);
        return;
    }
    switch (action) {
    case RemoteAction::Up:
        if (m_guide && !m_playing)
            m_guide->move(-1, 0);
        break;
    case RemoteAction::Down:
        if (m_guide && !m_playing)
            m_guide->move(1, 0);
        break;
    case RemoteAction::Left:
        if (m_guide && !m_playing)
            m_guide->move(0, -1);
        break;
    case RemoteAction::Right:
        if (m_guide && !m_playing)
            m_guide->move(0, 1);
        break;
    case RemoteAction::Ok:
        if (m_guide && !m_playing)
            m_guide->activate();
        break;
    case RemoteAction::PageUp:
        if (m_guide && !m_playing)
            m_guide->move(-1, 0);
        break;
    case RemoteAction::PageDown:
        if (m_guide && !m_playing)
            m_guide->move(1, 0);
        break;
    case RemoteAction::Back:
        if (m_playing)
            stopPlayback();
        else if (m_guide)
            m_guide->back();
        break;
    case RemoteAction::Menu:
        if (m_playing)
            stopPlayback();
        if (m_guide)
            m_guide->toggleMenu();
        break;
    case RemoteAction::PlayPause:
        if (m_playing)
            handleVoice(QStringLiteral("play"));
        else if (m_guide)
            m_guide->activate();
        break;
    case RemoteAction::VolumeUp:
        volume(1);
        break;
    case RemoteAction::VolumeDown:
        volume(-1);
        break;
    case RemoteAction::Mute:
        mute();
        break;
    case RemoteAction::Voice:
        m_voice->listen(m_dataDir);
        break;
    case RemoteAction::Power:
        m_samsung->togglePower();
        break;
    case RemoteAction::Ignore:
    case RemoteAction::Unmapped:
        break;
    }
}

void House::onSettings(int index, int verb)
{
    if (!m_samsung)
        return;
    switch (index) {
    case 2:
        if (verb == 0)
            m_samsung->refresh();
        break;
    case 3:
        if (verb == 0)
            m_samsung->togglePower();
        break;
    case 4:
        if (verb < 0)
            volume(-1);
        else if (verb > 0)
            volume(1);
        break;
    case 5:
        if (verb == 0)
            mute();
        break;
    case 6:
        if (verb == 0)
            m_samsung->sendHdmi();
        else
            m_samsung->nudgeHdmi(verb > 0 ? 1 : -1);
        break;
    default:
        break;
    }
}

} // namespace zappe
