#include "house.h"

#include "cdp.h"
#include "chrome.h"
#include "guide.h"
#include "logins.h"
#include "mirror.h"
#include "branding.h"
#include "epg.h"
#include "ota.h"
#include "posters.h"

#include <cstdio>
#include "remote.h"
#include "samsung.h"
#include "voice.h"
#include "xback.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>

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
    , m_mirror(new MirrorSession(this))
    , m_xback(new XBackGrab(this))
{
    connect(m_xback, &XBackGrab::pressed, this, &House::goHome);
    connect(m_chrome, &ChromeSession::failed, this, [this](const QString &message) {
        if (m_loading && !m_loadingFailed)
            failLoading(message);
        else if (!m_loading)
            toast(message);
    });
    connect(m_chrome, &ChromeSession::ready, this, [this] {
        if (!m_pendingUrl.isEmpty() && m_directPending) {
            m_directPending = false;
            m_direct->openPage(m_chrome->port(), QStringLiteral("about:blank"));
        }
    });
    connect(m_direct, &CdpClient::attached, this, [this] {
        if (m_pendingUrl.isEmpty())
            return;
        m_direct->navigate(m_pendingUrl);
    });
    connect(m_direct, &CdpClient::navigated, this, [this] {
        if (m_pendingUrl.isEmpty() || m_loadingFailed || !m_player)
            return;
        finishLoading();
        setPlaying(true);
        hideGuide();
    });
    connect(m_direct, &CdpClient::failed, this, [this](const QString &message) {
        if (m_loading && !m_loadingFailed)
            failLoading(message);
        else if (!m_loading)
            toast(message);
    });
    connect(m_direct, &CdpClient::historyResult, this, &House::onHistory);
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
                finishLoading();
                showGuide();
                emit changed();
            });
    connect(m_login, &LoginRunner::status, this, [this](const QString &message) {
        if (m_loading && !m_loadingFailed) {
            m_loadingStatus = message;
            emit changed();
        } else {
            toast(message);
        }
    });
    connect(m_login, &LoginRunner::failed, this, [this](const QString &message) {
        m_promptOpen = false;
        if (m_loading && !m_loadingFailed)
            failLoading(message);
        else {
            emit changed();
            toast(message);
            showGuide();
        }
    });
    connect(m_login, &LoginRunner::playing, this, [this](const QString &) {
        if (m_loadingFailed)
            return;
        m_promptOpen = false;
        finishLoading();
        emit changed();
        showGuide();
        if (m_guide && !m_guide->mirrorService().isEmpty())
            m_mirror->refresh(m_guide->mirrorService());
    });
    connect(m_login, &LoginRunner::historyResult, this, &House::onHistory);
    connect(m_ota, &OtaPlayer::started, this, [this](const QString &channel) {
        if (m_loadingFailed)
            return;
        finishLoading();
        toast(QStringLiteral("TV aberta · ") + channel);
        setPlaying(true);
        hideGuide();
    });
    connect(m_ota, &OtaPlayer::failed, this, [this](const QString &message) {
        if (m_loading && !m_loadingFailed)
            failLoading(message);
        else if (!m_loading)
            toast(message);
    });
    connect(m_ota, &OtaPlayer::frameReady, this, [this](const QString &channel, const QString &path) {
        if (!m_guide)
            return;
        const ArtPaths art = artFromFile(cacheDir(), path, channel, QStringLiteral("TV aberta"), true);
        m_guide->noteLiveFrame(QStringLiteral("zappe://ota/") + channel, art.artwork, art.ambient, art.color);
        if (m_loading && m_loadingKind == QLatin1String("ota") && m_loadingTitle == channel) {
            m_loadingArt = art.artwork;
            emit changed();
        }
    });
    connect(m_ota, &OtaPlayer::stopped, this, [this] {
        if (m_player && m_chrome->running())
            return;
        setPlaying(false);
        showGuide();
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
    connect(m_remote, &RemoteWatcher::dialKey, this, &House::onDial);
    connect(m_ota, &OtaPlayer::epgReady, this, &House::applyEpg);
    connect(m_chrome, &ChromeSession::exited, this, [this] { updateKeyHold(); });
    connect(m_mirror, &MirrorSession::playerReady, this, [this] {
        if (!m_player || m_loadingFailed)
            return;
        finishLoading();
        setPlaying(true);
        hideGuide();
        m_mirror->focusPlayer();
    });
    connect(m_mirror, &MirrorSession::needsLogin, this, [this](const QString &serviceId) {
        toast(QStringLiteral("Login necessário"));
        showGuide();
        openService(serviceId);
    });
    connect(m_mirror, &MirrorSession::failed, this, [this](const QString &message) {
        if (m_player && message.contains(QStringLiteral("about:blank"))) {
            cancelLoading();
            return;
        }
        if (m_player && m_loading && !m_loadingFailed)
            failLoading(message);
        else if (!m_loading)
            toast(message);
    });
}

void House::start(Guide *guide, QQuickWindow *window, const QString &dataDir)
{
    m_guide = guide;
    m_window = window;
    m_dataDir = dataDir;
    m_samsung->setDataDir(dataDir);
    connect(m_guide, &Guide::focusChanged, this, [this] {
        if (!m_focusTimer) {
            m_focusTimer = new QTimer(this);
            m_focusTimer->setSingleShot(true);
            m_focusTimer->setInterval(otaFocusRefreshMs());
            connect(m_focusTimer, &QTimer::timeout, this, &House::refreshFocusedArt);
        }
        m_focusTimer->start();
    });
    connect(m_guide, &Guide::settingsCommand, this, &House::onSettings);
    connect(m_guide, &Guide::pageChanged, this, [this] {
        if (m_guide && m_guide->page() == QLatin1String("settings"))
            m_samsung->refresh();
    });
    if (m_guide->page() == QLatin1String("settings"))
        m_samsung->refresh();
    connect(m_guide, &Guide::serviceRequested, this, &House::showMirror);
    connect(m_guide, &Guide::mirrorRetryRequested, this, [this](const QString &service) {
        if (m_mirror)
            m_mirror->refresh(service);
    });
    connect(m_guide, &Guide::profileRequested, this, [this](const QString &service, const QString &name) {
        showMirror(service);
        m_mirror->chooseProfile(service, name);
    });
    const QString preview = qEnvironmentVariable("ZAPPE_PREVIEW_LOADING").trimmed();
    if (!preview.isEmpty())
        beginLoading(preview, m_guide->focusArtwork(), QStringLiteral("ota"));
    m_remote->start();
    m_mirror->start(m_guide, m_chrome, dataDir);
    if (m_guide) {
        QStringList ids;
        for (const MirrorAdapter &adapter : loadMirrorAdapters(dataDir))
            ids.append(adapter.id);
        m_guide->setMirrorServiceIds(ids);
    }
    auto *front = new QTimer(this);
    front->setInterval(1000);
    connect(front, &QTimer::timeout, this, [this] {
        if (!m_playing)
            showGuide();
    });
    front->start();
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
    m_retryTitle = title;
    m_retryService = service;
    m_retryHref = href;
    const bool ota = service == QLatin1String("ota") || href.startsWith(QStringLiteral("zappe://ota/"));
    const QString epgStatus = ota && m_guide && m_guide->focusTitle() == title
                                      && m_guide->focusSynopsis().startsWith(QStringLiteral("Agora:"))
                                  ? m_guide->focusSynopsis()
                                  : QString();
    beginLoading(title, m_guide ? m_guide->focusArtwork() : QString(), ota ? QStringLiteral("ota") : QStringLiteral("chrome"));
    if (!epgStatus.isEmpty()) {
        m_loadingStatus = epgStatus;
        emit changed();
    }
    if (ota) {
        const QString query = href.startsWith(QStringLiteral("zappe://ota/")) ? href.mid(12) : title;
        playOta(query);
        return;
    }
    const QVector<MirrorAdapter> adapters = loadMirrorAdapters(m_dataDir);
    const MirrorAdapter *adapter = findMirrorAdapter(adapters, service);
    if (!adapter && m_guide)
        adapter = findMirrorAdapter(adapters, m_guide->mirrorService());
    if (adapter && mirrorIsPlayable(*adapter, href)) {
        if (!m_mirror->active()) {
            failLoading(QStringLiteral("Chrome não encontrado"));
            return;
        }
        m_player = true;
        m_chromeOwner = QStringLiteral("mirror");
        m_mirror->play(adapter->id, mirrorPlayUrl(*adapter, href));
        return;
    }
    if (adapter) {
        finishLoading();
        showMirror(adapter->id);
        return;
    }
    if (href.startsWith(QStringLiteral("http://")) || href.startsWith(QStringLiteral("https://"))) {
        if (href.contains(QStringLiteral("/watch")) || href.contains(QStringLiteral("watch?v="))) {
            m_player = true;
            m_chromeOwner = QStringLiteral("mirror");
            m_mirror->play(QString(), href);
            return;
        }
    }
    failLoading(QStringLiteral("Sem reprodução para ") + title);
}

void House::beginLoading(const QString &title, const QString &art, const QString &kind)
{
    m_loading = true;
    m_loadingFailed = false;
    m_loadingTitle = title;
    m_loadingArt = art;
    m_loadingKind = kind;
    m_loadingError.clear();
    m_loadingProgress = 0;
    m_loadingStatus = kind == QLatin1String("ota") ? QStringLiteral("Sintonizando…") : QStringLiteral("Abrindo…");
    m_loadStarted = QDateTime::currentMSecsSinceEpoch();
    if (!m_loadTimer) {
        m_loadTimer = new QTimer(this);
        m_loadTimer->setInterval(100);
        connect(m_loadTimer, &QTimer::timeout, this, &House::tickLoading);
    }
    m_loadTimer->start();
    showGuide();
    emit changed();
}

void House::tickLoading()
{
    if (!m_loading || m_loadingFailed)
        return;
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_loadStarted;
    m_loadingProgress = qMin(1.0, qreal(elapsed) / qreal(loadingTimeoutMs));
    showGuide();
    emit changed();
    if (elapsed < loadingTimeoutMs)
        return;
    m_ota->stop();
    m_chrome->stop();
    m_login->cancel();
    failLoading(m_loadingKind == QLatin1String("ota") ? QStringLiteral("A sintonia passou de 10 segundos.")
                                                      : QStringLiteral("O streaming passou de 10 segundos."));
}

void House::finishLoading()
{
    if (m_loadTimer)
        m_loadTimer->stop();
    if (!m_loading && !m_loadingFailed)
        return;
    m_loading = false;
    m_loadingFailed = false;
    m_loadingError.clear();
    m_loadingProgress = 0;
    emit changed();
}

void House::failLoading(const QString &message)
{
    if (m_loadTimer)
        m_loadTimer->stop();
    m_loading = true;
    m_loadingFailed = true;
    m_loadingError = message;
    m_loadingStatus = QStringLiteral("Não deu certo");
    showGuide();
    emit changed();
}

void House::cancelLoading()
{
    if (m_player) {
        leavePlayer();
        finishLoading();
        return;
    }
    m_login->cancel();
    m_promptOpen = false;
    m_pendingUrl.clear();
    m_ota->stop();
    m_chrome->stop();
    finishLoading();
    setPlaying(false);
    showGuide();
}

void House::retryLoading()
{
    if (m_retryTitle.isEmpty() && m_retryHref.isEmpty())
        return;
    playRequested(m_retryTitle, m_retryService, m_retryHref);
}

void House::onHistory(bool wentBack)
{
    if (wentBack)
        return;
    stopPlayback();
}

void House::chromeBack()
{
    if (m_chromeOwner == QLatin1String("direct"))
        m_direct->historyBack();
    else
        m_login->historyBack();
}

void House::goHome()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastBack < 150)
        return;
    m_lastBack = now;
    m_bandOffer = false;
    if (m_guide && !m_guide->dialText().isEmpty()) {
        m_guide->clearDial();
        if (m_dialTimer)
            m_dialTimer->stop();
    }
    if (m_loading)
        cancelLoading();
    if (m_promptOpen)
        cancelPrompt();
    if (m_player)
        leavePlayer();
    if (m_ota && m_ota->playing())
        stopPlayback();
    else if (m_playing)
        stopPlayback();
    if (m_guide)
        m_guide->home();
    showGuide();
}

void House::goBack()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastBack < 150)
        return;
    m_lastBack = now;
    if (m_bandOffer) {
        m_bandOffer = false;
        return;
    }
    if (m_guide && !m_guide->dialText().isEmpty()) {
        m_guide->clearDial();
        if (m_dialTimer)
            m_dialTimer->stop();
        if (m_ota->playing())
            hideGuide();
        return;
    }
    if (m_loading) {
        cancelLoading();
        return;
    }
    if (m_promptOpen) {
        cancelPrompt();
        return;
    }
    if (m_ota->playing()) {
        stopPlayback();
        return;
    }
    if (m_player) {
        leavePlayer();
        return;
    }
    if (m_playing && m_chrome->running()) {
        chromeBack();
        return;
    }
    if (m_playing) {
        stopPlayback();
        return;
    }
    if (m_guide)
        m_guide->back();
}

void House::leavePlayer()
{
    m_player = false;
    showGuide();
    m_mirror->leavePlayer();
    setPlaying(false);
}

void House::showMirror(const QString &serviceId)
{
    if (serviceId.isEmpty())
        return;
    if (!m_mirror->active()) {
        toast(QStringLiteral("Chrome não encontrado"));
        return;
    }
    if (m_player)
        leavePlayer();
    m_mirror->refresh(serviceId);
    if (m_guide)
        m_guide->openMirror(serviceId);
    showGuide();
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
    m_chromeOwner = QStringLiteral("login");
    connect(m_chrome, &ChromeSession::ready, this, [this, service = *service] {
        m_login->start(service, m_chrome->port(), m_dataDir);
    }, Qt::SingleShotConnection);
    m_chrome->ensure(m_dataDir);
}

void House::openUrl(const QString &url)
{
    m_pendingUrl = url;
    m_directPending = true;
    m_chromeOwner = QStringLiteral("direct");
    m_chrome->ensure(m_dataDir);
}

void House::playOta(const QString &query)
{
    m_chrome->stop();
    m_ota->play(query);
}

void House::setPlaying(bool playing)
{
    if (m_playing != playing) {
        m_playing = playing;
        emit changed();
    }
    updateKeyHold();
}

void House::updateKeyHold()
{
    // O mpv em tela cheia fica com o foco. O teclado do controle fica exclusivo
    // para Back e Escape fecharem o canal. O keycode X11 166 (XF86Back) também
    // é capturado aqui: o xdotool manda essa tecla para o mpv, que não conhece
    // o nome XF86Back.
    const bool otaOn = m_ota && m_ota->playing();
    m_remote->setHoldKeyboard(chromeFocused() || otaOn);
    if (m_xback)
        m_xback->setActive(otaOn);
}

bool House::chromeFocused() const
{
    return m_player && m_chrome->running() && !m_ota->playing();
}

void House::forwardChromeKey(RemoteAction action)
{
    QString name;
    switch (action) {
    case RemoteAction::Up:
        name = QStringLiteral("up");
        break;
    case RemoteAction::Down:
        name = QStringLiteral("down");
        break;
    case RemoteAction::Left:
        name = QStringLiteral("left");
        break;
    case RemoteAction::Right:
        name = QStringLiteral("right");
        break;
    case RemoteAction::Ok:
        name = QStringLiteral("ok");
        break;
    case RemoteAction::PageUp:
        name = QStringLiteral("up");
        break;
    case RemoteAction::PageDown:
        name = QStringLiteral("down");
        break;
    case RemoteAction::PlayPause:
        name = QStringLiteral("play");
        break;
    case RemoteAction::Back:
        name = QStringLiteral("back");
        break;
    default:
        return;
    }
    if (m_player) {
        m_mirror->dispatchAction(name);
        return;
    }
    if (m_chromeOwner == QLatin1String("direct"))
        m_direct->dispatchAction(name);
    else
        m_login->dispatchAction(name);
}

void House::showGuide()
{
    if (!m_window)
        return;
    if (!m_window->isVisible())
        m_window->showFullScreen();
    if (!m_window->isActive())
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
        goHome();
        break;
    case VoiceKind::Back:
        goBack();
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
        showMirror(intent.service);
        break;
    case VoiceKind::Search:
        if (intent.service.isEmpty())
            toast(QStringLiteral("Em qual streaming?"));
        else {
            showMirror(intent.service);
            m_mirror->search(intent.service, intent.query);
        }
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

void House::dial(const QString &glyph)
{
    if (m_promptOpen || chromeFocused() || !m_guide)
        return;
    m_guide->pushDial(glyph);
    if (m_guide->dialText().isEmpty())
        return;
    if (m_ota->playing())
        showGuide();
    if (!m_dialTimer) {
        m_dialTimer = new QTimer(this);
        m_dialTimer->setSingleShot(true);
        connect(m_dialTimer, &QTimer::timeout, this, &House::commitDial);
    }
    m_dialTimer->start(dialCommitMs());
}

void House::onDial(int value)
{
    if (value == 10)
        dial(QStringLiteral("."));
    else if (value >= 0 && value <= 9)
        dial(QString::number(value));
}

void House::commitDial()
{
    if (!m_guide)
        return;
    QString typed = m_guide->dialText().trimmed();
    m_guide->clearDial();
    if (typed.endsWith(QLatin1Char('.')))
        typed.chop(1);
    if (typed.isEmpty())
        return;
    const QVector<OtaChannel> channels = listOtaChannels();
    if (channels.isEmpty()) {
        toast(QStringLiteral("channels.conf não encontrado."));
        return;
    }
    const QString name = resolveDialTarget(typed, channels);
    if (name.isEmpty()) {
        const QString offer = bandRescanPrompt(typed);
        if (!offer.isEmpty()) {
            m_bandOffer = true;
            toast(offer);
            return;
        }
        toast(QStringLiteral("Canal %1 não está na varredura").arg(typed));
        return;
    }
    playRequested(name, QStringLiteral("ota"), QStringLiteral("zappe://ota/") + name);
}

bool House::acceptBandScan()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!m_bandOffer) {
        // O evdev e o Qt entregam o mesmo OK. O segundo não abre o card.
        return now - m_bandConsumed < 400;
    }
    m_bandOffer = false;
    m_bandConsumed = now;
    startBandScan();
    return true;
}

void House::startBandScan()
{
    if (m_bandScan && m_bandScan->state() != QProcess::NotRunning) {
        toast(QStringLiteral("A busca da Band já está em andamento."));
        return;
    }
    if (m_ota && !m_ota->releaseTuner()) {
        toast(QStringLiteral("Feche o canal no ar antes de buscar a Band."));
        return;
    }
    const QString bin = QStandardPaths::findExecutable(QStringLiteral("dvbv5-scan"));
    if (bin.isEmpty()) {
        toast(QStringLiteral("dvbv5-scan não está instalado."));
        return;
    }
    const QString dir = m_dataDir.isEmpty() ? resolveDataDir() : m_dataDir;
    QDir().mkpath(dir);
    const QString seedPath = QDir(dir).filePath(QStringLiteral("band-initial.conf"));
    const QString outPath = QDir(dir).filePath(QStringLiteral("band-scan.conf"));
    QFile seed(seedPath);
    if (!seed.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        toast(QStringLiteral("Não consegui gravar a busca da Band."));
        return;
    }
    seed.write(bandScanSeed().toUtf8());
    seed.close();
    QFile::remove(outPath);
    if (!m_bandScan) {
        m_bandScan = new QProcess(this);
        m_bandScan->setProcessChannelMode(QProcess::ForwardedChannels);
        connect(m_bandScan, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
                toast(QStringLiteral("Não abriu o dvbv5-scan."));
        });
        connect(m_bandScan, &QProcess::finished, this, [this](int, QProcess::ExitStatus) { finishBandScan(); });
    }
    m_bandScan->setProperty("outPath", outPath);
    m_bandScan->start(bin, bandScanArguments(seedPath, outPath));
    toast(QStringLiteral("Buscando a Band no canal físico %1…").arg(bandPhysicalChannel()));
    std::fprintf(stderr, "zappe: ota: rescan físico %d (%d Hz)\n", bandPhysicalChannel(), bandFrequencyHz());
}

void House::finishBandScan()
{
    const QString outPath = m_bandScan ? m_bandScan->property("outPath").toString() : QString();
    QString scanned;
    QFile out(outPath);
    if (out.open(QIODevice::ReadOnly))
        scanned = QString::fromUtf8(out.readAll());
    const QVector<OtaService> found = parseOtaServices(scanned);
    if (found.isEmpty()) {
        toast(QStringLiteral("A busca no canal físico %1 não achou canal.").arg(bandPhysicalChannel()));
        std::fprintf(stderr, "zappe: ota: rescan físico %d sem serviço\n", bandPhysicalChannel());
        return;
    }
    const QString dest = primaryChannelConfigPath();
    QString existing;
    QFile current(dest);
    if (current.open(QIODevice::ReadOnly))
        existing = QString::fromUtf8(current.readAll());
    const QString merged = mergeChannelConfig(existing, scanned);
    QDir().mkpath(QFileInfo(dest).absolutePath());
    QFile written(dest);
    if (!written.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        toast(QStringLiteral("Não consegui gravar o channels.conf."));
        return;
    }
    written.write(merged.toUtf8());
    written.close();
    bool band = false;
    for (const OtaService &service : found) {
        if (networkMarkId(service.name) == QLatin1String("band"))
            band = true;
    }
    std::fprintf(stderr, "zappe: ota: rescan físico %d gravou %d serviços em %s\n", bandPhysicalChannel(),
                 int(found.size()), qPrintable(dest));
    if (m_guide)
        m_guide->refreshLiveChannels();
    toast(band ? QStringLiteral("A Band entrou na varredura.")
               : QStringLiteral("A busca gravou %1 serviço(s), sem a Band no nome.").arg(found.size()));
}

void House::refreshFocusedArt()
{
    if (!m_guide || !m_guide->focusLive() || m_guide->page() != QLatin1String("home"))
        return;
    const QString title = m_guide->focusTitle();
    if (title.isEmpty())
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (title == m_refreshTitle && now - m_refreshAt < 20000)
        return;
    m_refreshTitle = title;
    m_refreshAt = now;
    m_ota->refreshFocused(title);
}

void House::applyEpg(const QString &channel, const QString &nowTitle, const QString &nextTitle)
{
    const QString line = epgGuideLine(nowTitle, nextTitle);
    if (line.isEmpty())
        return;
    if (m_guide)
        m_guide->noteLiveEpg(QStringLiteral("zappe://ota/") + channel, line);
    if (m_loading && m_loadingKind == QLatin1String("ota") && m_loadingTitle == channel) {
        m_loadingStatus = line;
        emit changed();
    }
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
    if (action == RemoteAction::Home) {
        goHome();
        return;
    }
    if (m_loading) {
        if (action == RemoteAction::Back)
            cancelLoading();
        else if (action == RemoteAction::Ok && m_loadingFailed)
            retryLoading();
        else if (action == RemoteAction::VolumeUp)
            volume(1);
        else if (action == RemoteAction::VolumeDown)
            volume(-1);
        else if (action == RemoteAction::Mute)
            mute();
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
        if (chromeFocused())
            forwardChromeKey(action);
        else if (m_guide && !m_playing)
            m_guide->move(-1, 0);
        break;
    case RemoteAction::Down:
        if (chromeFocused())
            forwardChromeKey(action);
        else if (m_guide && !m_playing)
            m_guide->move(1, 0);
        break;
    case RemoteAction::Left:
        if (chromeFocused())
            forwardChromeKey(action);
        else if (m_guide && !m_playing)
            m_guide->move(0, -1);
        break;
    case RemoteAction::Right:
        if (chromeFocused())
            forwardChromeKey(action);
        else if (m_guide && !m_playing)
            m_guide->move(0, 1);
        break;
    case RemoteAction::Ok:
        if (acceptBandScan())
            break;
        if (chromeFocused())
            forwardChromeKey(action);
        else if (m_guide && !m_playing)
            m_guide->activate();
        break;
    case RemoteAction::PageUp:
        if (chromeFocused())
            forwardChromeKey(action);
        else if (m_guide && !m_playing)
            m_guide->move(-1, 0);
        break;
    case RemoteAction::PageDown:
        if (chromeFocused())
            forwardChromeKey(action);
        else if (m_guide && !m_playing)
            m_guide->move(1, 0);
        break;
    case RemoteAction::Back:
        goBack();
        break;
    case RemoteAction::Home:
        goHome();
        break;
    case RemoteAction::Menu:
        if (m_playing)
            stopPlayback();
        if (m_guide)
            m_guide->toggleMenu();
        break;
    case RemoteAction::PlayPause:
        if (chromeFocused())
            forwardChromeKey(action);
        else if (m_playing)
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
