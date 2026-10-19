#include "voice.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace zappe {
namespace {

constexpr int kSampleRate = 16000;
constexpr int kMinSpeechSamples = kSampleRate / 5;

QString stripArticle(QString query)
{
    static const QRegularExpression article(
        QStringLiteral("^(a|o|as|os|na|no|nas|nos|da|do|das|dos|uma|um)\\s+"));
    return query.remove(article).trimmed();
}

bool has(const QString &text, const QString &pattern)
{
    return QRegularExpression(pattern).match(text).hasMatch();
}

QString serviceIn(const QString &text)
{
    if (has(text, QStringLiteral("\\bnetflix\\b")))
        return QStringLiteral("netflix");
    if (has(text, QStringLiteral("\\bgloboplay\\b")))
        return QStringLiteral("globoplay");
    if (has(text, QStringLiteral("\\b(prime\\s*video|prime|amazon)\\b")))
        return QStringLiteral("prime");
    if (has(text, QStringLiteral("\\b(disney\\s*\\+|disneyplus|disney\\s+plus|disney)\\b")))
        return QStringLiteral("disney");
    if (has(text, QStringLiteral("\\b(youtube|you\\s+tube)\\b")))
        return QStringLiteral("youtube");
    if (has(text, QStringLiteral("\\b(hbo\\s*max|hbo|max)\\b")))
        return QStringLiteral("hbo");
    if (has(text, QStringLiteral("\\b(apple\\s*tv|appletv|apple)\\b")))
        return QStringLiteral("apple");
    return {};
}

QString voicePrompt()
{
    return QStringLiteral("Abrir Netflix. Voltar. Volume mais. Mudo. Ir para Globo.");
}

bool envOff(const QString &value)
{
    const QString folded = value.trimmed().toLower();
    return folded == QLatin1String("off") || folded == QLatin1String("0") || folded == QLatin1String("false");
}

int voiceHoldCapMs()
{
    int seconds = qEnvironmentVariableIntValue("ZAPPE_VOICE_SECONDS");
    if (seconds < 2 || seconds > 8)
        seconds = 8;
    return seconds * 1000;
}

void append16(QByteArray &out, quint16 value)
{
    out.append(char(value & 0xff));
    out.append(char((value >> 8) & 0xff));
}

void append32(QByteArray &out, quint32 value)
{
    out.append(char(value & 0xff));
    out.append(char((value >> 8) & 0xff));
    out.append(char((value >> 16) & 0xff));
    out.append(char((value >> 24) & 0xff));
}

QString firstExecutable(const QStringList &candidates, const QString &onPath)
{
    for (const QString &candidate : candidates) {
        if (QFileInfo(candidate).isExecutable())
            return candidate;
    }
    return QStandardPaths::findExecutable(onPath);
}

QUrl whisperBaseUrl()
{
    QString raw = qEnvironmentVariable("ZAPPE_WHISPER_URL").trimmed();
    if (raw.isEmpty())
        raw = QStringLiteral("http://127.0.0.1:%1").arg(whisperServerPort());
    while (raw.endsWith(QLatin1Char('/')))
        raw.chop(1);
    if (raw.endsWith(QLatin1String("/inference")))
        raw.chop(QStringLiteral("/inference").size());
    if (raw.endsWith(QLatin1String("/health")))
        raw.chop(QStringLiteral("/health").size());
    return QUrl(raw);
}

QByteArray readRawPcm(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QByteArray pcm = file.readAll();
    if (pcm.size() % 2)
        pcm.chop(1);
    return pcm;
}

bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(bytes) == bytes.size();
}

} // namespace

QString foldVoice(const QString &text)
{
    QString folded = text.toLower().normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(folded.size());
    for (const QChar ch : folded) {
        if (ch.category() == QChar::Mark_NonSpacing)
            continue;
        if (ch.isLetterOrNumber() || ch.isSpace())
            out.append(ch);
        else
            out.append(QLatin1Char(' '));
    }
    return out.simplified();
}

QStringList voiceLexicon()
{
    static const QStringList words = {
        QStringLiteral("aumenta"),   QStringLiteral("aumentar"),  QStringLiteral("alto"),
        QStringLiteral("volume"),    QStringLiteral("mais"),      QStringLiteral("sobe"),
        QStringLiteral("diminui"),   QStringLiteral("diminuir"),  QStringLiteral("abaixa"),
        QStringLiteral("menos"),     QStringLiteral("baixo"),     QStringLiteral("desce"),
        QStringLiteral("mudo"),      QStringLiteral("mutar"),     QStringLiteral("desmutar"),
        QStringLiteral("unmute"),    QStringLiteral("silencio"),  QStringLiteral("som"),
        QStringLiteral("tira"),      QStringLiteral("voltar"),    QStringLiteral("volta"),
        QStringLiteral("retornar"),  QStringLiteral("retorna"),   QStringLiteral("back"),
        QStringLiteral("pausar"),    QStringLiteral("pause"),     QStringLiteral("parar"),
        QStringLiteral("stop"),      QStringLiteral("continuar"), QStringLiteral("play"),
        QStringLiteral("reproduzir"), QStringLiteral("tocar"),    QStringLiteral("abrir"),
        QStringLiteral("abre"),      QStringLiteral("buscar"),    QStringLiteral("busca"),
        QStringLiteral("procurar"),  QStringLiteral("procura"),   QStringLiteral("pesquisar"),
        QStringLiteral("pesquisa"),  QStringLiteral("ir"),        QStringLiteral("para"),
        QStringLiteral("vai"),       QStringLiteral("pra"),       QStringLiteral("passa"),
        QStringLiteral("troca"),     QStringLiteral("poe"),       QStringLiteral("ponha"),
        QStringLiteral("coloca"),    QStringLiteral("assiste"),   QStringLiteral("assistir"),
        QStringLiteral("canal"),     QStringLiteral("netflix"),   QStringLiteral("globoplay"),
        QStringLiteral("prime"),     QStringLiteral("amazon"),    QStringLiteral("disney"),
        QStringLiteral("youtube"),   QStringLiteral("hbo"),       QStringLiteral("max"),
        QStringLiteral("apple"),     QStringLiteral("globo"),     QStringLiteral("sbt"),
        QStringLiteral("record"),    QStringLiteral("band"),      QStringLiteral("cultura"),
        QStringLiteral("inicio"),    QStringLiteral("home"),      QStringLiteral("guia"),
        QStringLiteral("principal"), QStringLiteral("tela"),      QStringLiteral("inicial"),
        QStringLiteral("video"),     QStringLiteral("plus"),
    };
    return words;
}

int voiceEditDistance(const QString &left, const QString &right)
{
    const int rows = int(left.size());
    const int cols = int(right.size());
    if (rows == 0)
        return cols;
    if (cols == 0)
        return rows;
    std::vector<int> prev(size_t(cols + 1));
    std::vector<int> cur(size_t(cols + 1));
    for (int col = 0; col <= cols; ++col)
        prev[size_t(col)] = col;
    for (int row = 1; row <= rows; ++row) {
        cur[0] = row;
        for (int col = 1; col <= cols; ++col) {
            const int cost = left.at(row - 1) == right.at(col - 1) ? 0 : 1;
            cur[size_t(col)] = std::min({prev[size_t(col)] + 1, cur[size_t(col - 1)] + 1, prev[size_t(col - 1)] + cost});
        }
        prev.swap(cur);
    }
    return prev[size_t(cols)];
}

QString fuzzyCorrectVoice(const QString &text)
{
    const QString folded = foldVoice(text);
    if (folded.isEmpty())
        return folded;
    const QStringList lexicon = voiceLexicon();
    QStringList out;
    for (const QString &word : folded.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (lexicon.contains(word)) {
            out.append(word);
            continue;
        }
        const int cap = word.size() <= 3 ? 0 : (word.size() == 4 ? 1 : 2);
        if (cap == 0) {
            out.append(word);
            continue;
        }
        int best = cap + 1;
        int hits = 0;
        QString pick;
        for (const QString &candidate : lexicon) {
            const int gap = std::abs(int(candidate.size()) - int(word.size()));
            if (gap > cap)
                continue;
            const int dist = voiceEditDistance(word, candidate);
            if (dist == 0 || dist > cap)
                continue;
            if (dist < best) {
                best = dist;
                hits = 1;
                pick = candidate;
            } else if (dist == best) {
                ++hits;
            }
        }
        out.append(hits == 1 ? pick : word);
    }
    return out.join(QLatin1Char(' '));
}

VoiceIntent parseVoicePtBr(const QString &text)
{
    const QString folded = fuzzyCorrectVoice(text);
    if (folded.isEmpty())
        return {};

    if (has(folded, QStringLiteral("\\b(mudo|mutar|unmute|desmutar|silencio|sem\\s+som|tira\\s+o\\s+mudo)\\b")))
        return {VoiceKind::Mute, {}, {}, 0};
    if (has(folded, QStringLiteral("\\b(volume\\s+(mais|aumentar|alto)|aumentar\\s+o?\\s*volume|aumenta\\s+o?\\s*"
                                    "volume|mais\\s+alto|sobe\\s+o?\\s*volume)\\b")))
        return {VoiceKind::Volume, {}, {}, 1};
    if (has(folded, QStringLiteral("\\b(volume\\s+(menos|diminuir|baixo)|diminuir\\s+o?\\s*volume|diminui\\s+o?\\s*"
                                    "volume|abaixa\\s+o?\\s*volume|mais\\s+baixo|desce\\s+o?\\s*volume)\\b")))
        return {VoiceKind::Volume, {}, {}, -1};

    static const QRegularExpression search(QStringLiteral(
        "\\b(?:buscar|busca|procurar|procura|pesquisar|pesquisa)\\s+(.+)$"));
    const QRegularExpressionMatch found = search.match(folded);
    if (found.hasMatch()) {
        QString rest = found.captured(1).trimmed();
        const QString service = serviceIn(rest);
        if (!service.isEmpty()) {
            rest.remove(QRegularExpression(QStringLiteral(
                "\\b(netflix|globoplay|prime\\s*video|prime|amazon|disney\\s*\\+|disneyplus|disney\\s+plus|"
                "disney|youtube|you\\s+tube|hbo\\s*max|hbo|max|apple\\s*tv|appletv|apple)\\b")));
            rest.remove(QRegularExpression(QStringLiteral("\\b(na|no|em|do|da|de)\\b")));
            rest = rest.simplified();
        }
        if (rest.isEmpty() && !service.isEmpty())
            return {VoiceKind::Open, service, {}, 0};
        return {VoiceKind::Search, service, rest, 0};
    }

    if (const QString service = serviceIn(folded); !service.isEmpty())
        return {VoiceKind::Open, service, {}, 0};

    if (has(folded, QStringLiteral("\\b(pausar|pause|parar|stop|continuar|play|reproduzir|tocar)\\b")))
        return {VoiceKind::PlayPause, {}, {}, 0};
    if (has(folded, QStringLiteral("\\b(voltar|volta|retornar|retorna|back)\\b")))
        return {VoiceKind::Back, {}, {}, 0};

    static const QRegularExpression go(QStringLiteral(
        "\\b(?:ir\\s+para|vai\\s+para|vai\\s+pra|passa\\s+para|passa\\s+pra|troca\\s+para|troca\\s+pra|poe|"
        "ponha|coloca|abrir|abre|assiste|assistir|canal)\\s+(.+)$"));
    const QRegularExpressionMatch match = go.match(folded);
    if (match.hasMatch()) {
        const QString query = stripArticle(match.captured(1));
        if (has(query, QStringLiteral("^(inicio|home|guia|principal|tela\\s+inicial)$")))
            return {VoiceKind::Home, {}, {}, 0};
        if (const QString service = serviceIn(query); !service.isEmpty())
            return {VoiceKind::Open, service, {}, 0};
        if (!query.isEmpty())
            return {VoiceKind::Ota, {}, query, 0};
    }

    static const QRegularExpression bare(QStringLiteral("\\b(globo|sbt|record|band|cultura)\\b"));
    const QRegularExpressionMatch channel = bare.match(folded);
    if (channel.hasMatch())
        return {VoiceKind::Ota, {}, channel.captured(1), 0};

    if (has(folded, QStringLiteral("\\b(inicio|home|guia|principal|tela\\s+inicial)\\b")))
        return {VoiceKind::Home, {}, {}, 0};
    return {};
}

int whisperAudioCtx(int clipMs)
{
    if (clipMs < 0)
        clipMs = 0;
    const int ctx = clipMs / 20 + 64;
    if (ctx < 256)
        return 256;
    if (ctx > 1500)
        return 1500;
    return ctx;
}

int whisperPartialIntervalMs()
{
    return 1000;
}

int whisperServerPort()
{
    const int port = qEnvironmentVariableIntValue("ZAPPE_WHISPER_PORT");
    if (port < 1 || port > 65535)
        return 8178;
    return port;
}

QStringList whisperCliArgs(const QString &model, const QString &wav, int audioCtx)
{
    return {QStringLiteral("-m"),
            model,
            QStringLiteral("-f"),
            wav,
            QStringLiteral("-l"),
            QStringLiteral("pt"),
            QStringLiteral("-nt"),
            QStringLiteral("-t"),
            QStringLiteral("4"),
            QStringLiteral("-bs"),
            QStringLiteral("1"),
            QStringLiteral("-bo"),
            QStringLiteral("1"),
            QStringLiteral("-ac"),
            QString::number(audioCtx),
            QStringLiteral("--prompt"),
            voicePrompt()};
}

QProcessEnvironment whisperChildEnvironment(const QProcessEnvironment &parent)
{
    QProcessEnvironment env = parent;
    env.insert(QStringLiteral("ENABLE_GAMESCOPE_WSI"), QStringLiteral("0"));
    return env;
}

QStringList whisperServerArgs(const QString &model, int port)
{
    return {QStringLiteral("-m"),
            model,
            QStringLiteral("-l"),
            QStringLiteral("pt"),
            QStringLiteral("-nt"),
            QStringLiteral("-t"),
            QStringLiteral("4"),
            QStringLiteral("-bs"),
            QStringLiteral("1"),
            QStringLiteral("-bo"),
            QStringLiteral("1"),
            QStringLiteral("--host"),
            QStringLiteral("127.0.0.1"),
            QStringLiteral("--port"),
            QString::number(port),
            QStringLiteral("--convert")};
}

SilenceTrim trimSilencePcm(const qint16 *samples, int count, int sampleRate)
{
    if (!samples || count <= 0)
        return {};
    if (sampleRate <= 0)
        return {0, count};
    int frame = sampleRate / 50;
    if (frame < 1)
        frame = 1;
    const int frames = (count + frame - 1) / frame;
    std::vector<double> rms(size_t(frames), 0);
    double peak = 0;
    for (int index = 0; index < frames; ++index) {
        const int begin = index * frame;
        const int length = std::min(frame, count - begin);
        qint64 acc = 0;
        for (int sample = 0; sample < length; ++sample) {
            const qint64 value = samples[begin + sample];
            acc += value * value;
        }
        const double energy = length > 0 ? std::sqrt(double(acc) / double(length)) : 0;
        rms[size_t(index)] = energy;
        peak = std::max(peak, energy);
    }
    const double threshold = std::max(180.0, peak * 0.08);
    int first = -1;
    int last = -1;
    for (int index = 0; index < frames; ++index) {
        if (rms[size_t(index)] < threshold)
            continue;
        if (first < 0)
            first = index;
        last = index;
    }
    if (first < 0)
        return {0, count};
    const int pad = sampleRate / 10;
    int start = first * frame - pad;
    if (start < 0)
        start = 0;
    int end = (last + 1) * frame + pad;
    if (end > count)
        end = count;
    if (end < start)
        end = start;
    return {start, end - start};
}

QByteArray wavFromPcm(const QByteArray &pcm, int sampleRate)
{
    if (sampleRate <= 0)
        sampleRate = kSampleRate;
    const auto dataSize = quint32(std::max(0, int(pcm.size())));
    const auto rate = quint32(sampleRate);
    QByteArray out;
    out.reserve(44 + int(dataSize));
    out.append("RIFF");
    append32(out, 36u + dataSize);
    out.append("WAVE");
    out.append("fmt ");
    append32(out, 16);
    append16(out, 1);
    append16(out, 1);
    append32(out, rate);
    append32(out, rate * 2);
    append16(out, 2);
    append16(out, 16);
    out.append("data");
    append32(out, dataSize);
    out.append(pcm);
    return out;
}

QString whisperTranscriptFrom(const QByteArray &stdoutBytes, const QByteArray &stderrBytes)
{
    const QByteArray trimmed = stdoutBytes.trimmed();
    if (trimmed.startsWith('{')) {
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed);
        if (doc.isObject() && doc.object().contains(QStringLiteral("text")))
            return doc.object().value(QStringLiteral("text")).toString().trimmed();
    }
    const QString raw = QString::fromUtf8(stdoutBytes) + QString::fromUtf8(stderrBytes);
    QStringList lines;
    for (const QString &line : raw.split(QLatin1Char('\n'))) {
        QString cleaned = line.trimmed();
        if (cleaned.startsWith(QLatin1Char('['))) {
            const int end = cleaned.indexOf(QLatin1Char(']'));
            if (end >= 0)
                cleaned = cleaned.mid(end + 1).trimmed();
        }
        if (cleaned.isEmpty() || cleaned.contains(QStringLiteral("whisper.cpp"), Qt::CaseInsensitive))
            continue;
        if (cleaned.startsWith(QLatin1String("error:"), Qt::CaseInsensitive))
            continue;
        lines.append(cleaned);
    }
    return lines.join(QLatin1Char(' ')).trimmed();
}

WhisperLayout whisperLayout(const QString &dataDir)
{
    WhisperLayout layout;
    const QString home = QDir::homePath() + QStringLiteral("/.local/share/zappe");
    QString root = qEnvironmentVariable("ZAPPE_WHISPER_DIR").trimmed();
    if (root.isEmpty())
        root = QDir(dataDir.isEmpty() ? home : dataDir).filePath(QStringLiteral("whisper"));
    layout.root = root;
    layout.binDir = QDir(root).filePath(QStringLiteral("bin"));

    const QString explicitModel = qEnvironmentVariable("ZAPPE_WHISPER_MODEL").trimmed();
    if (!explicitModel.isEmpty() && QFile::exists(explicitModel)) {
        layout.model = explicitModel;
    } else {
        const QStringList names = {QStringLiteral("models/ggml-small-q8_0.bin"),
                                   QStringLiteral("ggml-small-q8_0.bin"),
                                   QStringLiteral("ggml-small.bin"),
                                   QStringLiteral("ggml-base.bin"),
                                   QStringLiteral("ggml-tiny.bin"),
                                   QStringLiteral("ggml-base-q5_1.bin"),
                                   QStringLiteral("ggml-tiny-q5_1.bin")};
        for (const QString &name : names) {
            const QString path = QDir(root).filePath(name);
            if (QFile::exists(path)) {
                layout.model = path;
                break;
            }
        }
    }

    const QString serverEnv = qEnvironmentVariable("ZAPPE_WHISPER_SERVER").trimmed();
    if (envOff(serverEnv)) {
        layout.serverDisabled = true;
    } else if (!serverEnv.isEmpty()) {
        layout.serverBin = QFileInfo(serverEnv).isExecutable() ? serverEnv
                                                               : QStandardPaths::findExecutable(serverEnv);
    } else {
        layout.serverBin = firstExecutable({QDir(layout.binDir).filePath(QStringLiteral("whisper-server")),
                                            QDir(root).filePath(QStringLiteral("whisper-server")),
                                            QDir::homePath() + QStringLiteral("/bin/whisper-server")},
                                           QStringLiteral("whisper-server"));
    }

    const QString cliEnv = qEnvironmentVariable("ZAPPE_WHISPER_BIN").trimmed();
    if (!cliEnv.isEmpty()) {
        layout.cliBin = QFileInfo(cliEnv).isExecutable() ? cliEnv : QStandardPaths::findExecutable(cliEnv);
    } else {
        layout.cliBin = firstExecutable({QDir(layout.binDir).filePath(QStringLiteral("whisper-cli")),
                                         QDir(root).filePath(QStringLiteral("whisper-cli")),
                                         QDir::homePath() + QStringLiteral("/bin/whisper-cli")},
                                        QStringLiteral("whisper-cli"));
    }
    return layout;
}

VoiceSession::VoiceSession(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
}

VoiceSession::~VoiceSession()
{
    if (m_record && m_record->state() != QProcess::NotRunning)
        m_record->kill();
    if (m_server && m_server->state() != QProcess::NotRunning)
        m_server->terminate();
}

void VoiceSession::warm(const QString &dataDir)
{
    m_dataDir = dataDir;
    if (!qEnvironmentVariable("ZAPPE_VOICE_FAKE").isEmpty())
        return;
    ensureServer();
}

void VoiceSession::press(const QString &dataDir)
{
    if (m_holding)
        return;
    m_dataDir = dataDir;
    ++m_generation;
    m_finalPending = false;
    m_holding = true;
    m_busy = true;
    m_fake = false;
    m_holdStart = QDateTime::currentMSecsSinceEpoch();
    emit started();
    if (const QString fake = qEnvironmentVariable("ZAPPE_VOICE_FAKE"); !fake.isEmpty()) {
        m_fake = true;
        m_fakeText = fake;
        return;
    }
    ensureServer();
    startRecord();
    if (!m_holding)
        return;
    if (!m_partialTimer) {
        m_partialTimer = new QTimer(this);
        m_partialTimer->setInterval(whisperPartialIntervalMs());
        connect(m_partialTimer, &QTimer::timeout, this, &VoiceSession::onPartialTick);
    }
    m_partialTimer->start();
}

void VoiceSession::release()
{
    if (!m_holding)
        return;
    m_holding = false;
    if (m_partialTimer)
        m_partialTimer->stop();
    if (m_fake) {
        m_busy = false;
        emit finished(fuzzyCorrectVoice(m_fakeText));
        return;
    }
    m_finalPending = true;
    if (m_record && m_record->state() != QProcess::NotRunning) {
        m_stopRequested = true;
        m_record->terminate();
        QTimer::singleShot(800, this, [this] {
            if (m_record && m_finalPending && m_record->state() != QProcess::NotRunning)
                m_record->kill();
        });
        return;
    }
    if (!m_inflight)
        kickFinal();
}

void VoiceSession::cancel()
{
    ++m_generation;
    m_holding = false;
    m_busy = false;
    m_finalPending = false;
    m_inflight = false;
    if (m_partialTimer)
        m_partialTimer->stop();
    stopRecord();
}

void VoiceSession::ensureServer()
{
    if (m_serverReady || m_serverStarting)
        return;
    const WhisperLayout layout = whisperLayout(m_dataDir);
    if (layout.serverDisabled) {
        std::fprintf(stderr, "zappe: voz: whisper-server desligado, usando whisper-cli\n");
        return;
    }
    if (layout.model.isEmpty()) {
        std::fprintf(stderr, "zappe: voz: modelo Whisper ausente em %s\n", qPrintable(layout.root));
        return;
    }
    const bool external = !qEnvironmentVariable("ZAPPE_WHISPER_URL").trimmed().isEmpty();
    if (!external && layout.serverBin.isEmpty()) {
        std::fprintf(stderr, "zappe: voz: whisper-server ausente, usando whisper-cli\n");
        return;
    }
    m_serverStarting = true;
    m_healthTries = 0;
    if (!m_healthTimer) {
        m_healthTimer = new QTimer(this);
        m_healthTimer->setInterval(500);
        connect(m_healthTimer, &QTimer::timeout, this, &VoiceSession::pollHealth);
    }
    pollHealth();
    m_healthTimer->start();
}

void VoiceSession::pollHealth()
{
    if (m_serverReady || m_healthBusy || !m_net)
        return;
    if (m_healthTries++ > 60) {
        if (m_healthTimer)
            m_healthTimer->stop();
        m_serverStarting = false;
        std::fprintf(stderr, "zappe: voz: whisper-server não ficou pronto, usando whisper-cli\n");
        return;
    }
    QUrl url = whisperBaseUrl();
    url.setPath(QStringLiteral("/health"));
    QNetworkRequest request(url);
    request.setTransferTimeout(1500);
    m_healthBusy = true;
    QNetworkReply *reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_healthBusy = false;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if (reply->error() == QNetworkReply::NoError && status == 200 && body.contains("\"ok\"")) {
            m_serverReady = true;
            m_serverStarting = false;
            if (m_healthTimer)
                m_healthTimer->stop();
            std::fprintf(stderr, "zappe: voz: whisper-server pronto\n");
            return;
        }
        if (reply->error() == QNetworkReply::ConnectionRefusedError || reply->error() == QNetworkReply::HostNotFoundError)
            spawnServer();
    });
}

void VoiceSession::spawnServer()
{
    if (m_spawned || m_server)
        return;
    if (!qEnvironmentVariable("ZAPPE_WHISPER_URL").trimmed().isEmpty())
        return;
    const WhisperLayout layout = whisperLayout(m_dataDir);
    if (layout.serverDisabled || layout.serverBin.isEmpty() || layout.model.isEmpty())
        return;
    m_spawned = true;
    auto *proc = new QProcess(this);
    proc->setWorkingDirectory(QDir::tempPath());
    proc->setProcessEnvironment(whisperChildEnvironment(QProcessEnvironment::systemEnvironment()));
    proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus status) {
        if (m_server == proc)
            m_server = nullptr;
        m_serverReady = false;
        if (status != QProcess::NormalExit || code != 0) {
            const QByteArray tail = proc->readAll().right(400);
            std::fprintf(stderr, "zappe: voz: whisper-server encerrou (%d) %s\n", code, tail.constData());
        }
        proc->deleteLater();
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        std::fprintf(stderr, "zappe: voz: whisper-server não iniciou\n");
        if (m_server == proc)
            m_server = nullptr;
        proc->deleteLater();
    });
    const int port = whisperServerPort();
    std::fprintf(stderr, "zappe: voz: whisper-server %s porta %d\n", qPrintable(layout.serverBin), port);
    proc->start(layout.serverBin, whisperServerArgs(layout.model, port));
    m_server = proc;
}

void VoiceSession::startRecord()
{
    if (m_record && m_record->state() != QProcess::NotRunning) {
        m_stopRequested = true;
        m_record->kill();
        m_record = nullptr;
    }
    m_stopRequested = false;
    m_rawPath = QDir(QDir::tempPath()).filePath(QStringLiteral("zappe-voice.raw"));
    QFile::remove(m_rawPath);
    QStringList args = {QStringLiteral("-q"),       QStringLiteral("-f"), QStringLiteral("S16_LE"),
                        QStringLiteral("-r"),       QStringLiteral("16000"), QStringLiteral("-c"),
                        QStringLiteral("1"),        QStringLiteral("-t"), QStringLiteral("raw")};
    if (const QString device = qEnvironmentVariable("ZAPPE_ALSA_DEVICE"); !device.isEmpty())
        args << QStringLiteral("-D") << device;
    args << m_rawPath;

    auto *record = new QProcess(this);
    m_record = record;
    connect(record, &QProcess::finished, this, [this, record](int code, QProcess::ExitStatus status) {
        const bool current = m_record == record;
        if (current)
            m_record = nullptr;
        record->deleteLater();
        if (!current)
            return;
        const bool stopped = m_stopRequested;
        m_stopRequested = false;
        if (m_holding && !stopped) {
            m_holding = false;
            m_busy = false;
            m_finalPending = false;
            if (m_partialTimer)
                m_partialTimer->stop();
            emit failed(QStringLiteral("Microfone indisponível (arecord)."));
            return;
        }
        Q_UNUSED(code);
        Q_UNUSED(status);
        kickFinal();
    });
    record->start(QStringLiteral("arecord"), args);
    if (!record->waitForStarted(1500)) {
        m_record = nullptr;
        record->kill();
        record->deleteLater();
        m_holding = false;
        m_busy = false;
        if (m_partialTimer)
            m_partialTimer->stop();
        emit failed(QStringLiteral("arecord não iniciou."));
    }
}

void VoiceSession::stopRecord()
{
    if (!m_record || m_record->state() == QProcess::NotRunning)
        return;
    m_stopRequested = true;
    m_record->terminate();
}

void VoiceSession::onPartialTick()
{
    if (!m_holding)
        return;
    if (m_holdStart > 0 && QDateTime::currentMSecsSinceEpoch() - m_holdStart >= voiceHoldCapMs()) {
        release();
        return;
    }
    if (m_inflight || m_fake)
        return;
    transcribeFile(false);
}

void VoiceSession::kickFinal()
{
    if (!m_finalPending || m_inflight || m_record)
        return;
    m_finalPending = false;
    transcribeFile(true);
}

void VoiceSession::transcribeFile(bool finalPass)
{
    if (m_inflight)
        return;
    const int generation = m_generation;
    const QByteArray pcm = readRawPcm(m_rawPath);
    const auto *samples = reinterpret_cast<const qint16 *>(pcm.constData());
    const int count = pcm.size() / int(sizeof(qint16));
    const SilenceTrim trim = trimSilencePcm(samples, count, kSampleRate);
    if (trim.sampleCount < kMinSpeechSamples) {
        if (!finalPass)
            return;
        m_busy = false;
        std::fprintf(stderr, "zappe: voz: final sem fala depois do corte\n");
        emit failed(QStringLiteral("Não ouvi."));
        return;
    }
    const QByteArray sliced = pcm.mid(trim.startSample * int(sizeof(qint16)), trim.sampleCount * int(sizeof(qint16)));
    const int clipMs = int(qint64(trim.sampleCount) * 1000 / kSampleRate);
    const int audioCtx = whisperAudioCtx(clipMs);
    m_wavPath = QDir(QDir::tempPath()).filePath(QStringLiteral("zappe-voice.wav"));
    if (!writeBytes(m_wavPath, wavFromPcm(sliced, kSampleRate))) {
        if (finalPass) {
            m_busy = false;
            emit failed(QStringLiteral("Não ouvi."));
        }
        return;
    }

    const int requestId = ++m_requestId;
    const qint64 startedMs = QDateTime::currentMSecsSinceEpoch();
    m_inflight = true;
    if (!m_serverReady) {
        runCli(m_wavPath, finalPass, generation, requestId, audioCtx, startedMs);
        return;
    }

    const QByteArray wavBytes = wavFromPcm(sliced, kSampleRate);
    auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    auto addField = [multi](const QString &name, const QByteArray &value) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"%1\"").arg(name)));
        part.setBody(value);
        multi->append(part);
    };
    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QStringLiteral("audio/wav")));
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"file\"; filename=\"clip.wav\"")));
    filePart.setBody(wavBytes);
    multi->append(filePart);
    addField(QStringLiteral("response_format"), QByteArrayLiteral("json"));
    addField(QStringLiteral("language"), QByteArrayLiteral("pt"));
    addField(QStringLiteral("audio_ctx"), QByteArray::number(audioCtx));
    addField(QStringLiteral("beam_size"), QByteArrayLiteral("1"));
    addField(QStringLiteral("best_of"), QByteArrayLiteral("1"));
    addField(QStringLiteral("no_timestamps"), QByteArrayLiteral("true"));
    addField(QStringLiteral("prompt"), voicePrompt().toUtf8());

    QUrl url = whisperBaseUrl();
    url.setPath(QStringLiteral("/inference"));
    QNetworkRequest request(url);
    request.setTransferTimeout(finalPass ? 20000 : 8000);
    QNetworkReply *reply = m_net->post(request, multi);
    multi->setParent(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, finalPass, generation, requestId, startedMs, audioCtx] {
        const QByteArray body = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        finishRequest(finalPass, generation, requestId, startedMs, audioCtx, body, {}, ok, false);
    });
}

void VoiceSession::runCli(const QString &wav, bool finalPass, int generation, int requestId, int audioCtx,
                          qint64 startedMs)
{
    const WhisperLayout layout = whisperLayout(m_dataDir);
    if (layout.model.isEmpty() || layout.cliBin.isEmpty()) {
        finishRequest(finalPass, generation, requestId, startedMs, audioCtx, {}, {}, false, true);
        return;
    }
    m_inflight = true;
    auto *whisper = new QProcess(this);
    whisper->setProcessEnvironment(whisperChildEnvironment(QProcessEnvironment::systemEnvironment()));
    connect(whisper, &QProcess::finished, this,
            [this, whisper, finalPass, generation, requestId, startedMs, audioCtx](int code, QProcess::ExitStatus status) {
                const QByteArray out = whisper->readAllStandardOutput();
                const QByteArray err = whisper->readAllStandardError();
                whisper->deleteLater();
                const bool ok = status == QProcess::NormalExit && code == 0;
                finishRequest(finalPass, generation, requestId, startedMs, audioCtx, out, err, ok, true);
            });
    whisper->start(layout.cliBin, whisperCliArgs(layout.model, wav, audioCtx));
    if (!whisper->waitForStarted(1500)) {
        whisper->disconnect();
        whisper->kill();
        whisper->deleteLater();
        finishRequest(finalPass, generation, requestId, startedMs, audioCtx, {}, {}, false, true);
    }
}

void VoiceSession::finishRequest(bool finalPass, int generation, int requestId, qint64 startedMs, int audioCtx,
                                 const QByteArray &body, const QByteArray &err, bool ok, bool fromCli)
{
    if (requestId != m_requestId)
        return;
    m_inflight = false;
    if (generation != m_generation) {
        kickFinal();
        return;
    }
    if (!ok && !fromCli) {
        const WhisperLayout layout = whisperLayout(m_dataDir);
        if (!layout.cliBin.isEmpty() && !layout.model.isEmpty() && QFile::exists(m_wavPath)) {
            std::fprintf(stderr, "zappe: voz: whisper-server falhou, usando whisper-cli\n");
            runCli(m_wavPath, finalPass, generation, requestId, audioCtx, startedMs);
            return;
        }
    }
    const qint64 ms = QDateTime::currentMSecsSinceEpoch() - startedMs;
    QString text = ok ? fuzzyCorrectVoice(whisperTranscriptFrom(body, err)) : QString();
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    std::fprintf(stderr, "zappe: voz: %s %lld ms ac=%d \"%s\"\n", finalPass ? "final" : "parcial",
                 static_cast<long long>(ms), audioCtx, qPrintable(text));
    if (!ok) {
        if (finalPass) {
            m_busy = false;
            const bool missing = whisperLayout(m_dataDir).model.isEmpty();
            emit failed(missing ? QStringLiteral("Modelo Whisper não instalado.")
                                : QStringLiteral("Whisper falhou."));
        }
    } else if (text.isEmpty()) {
        if (finalPass) {
            m_busy = false;
            emit failed(QStringLiteral("Não ouvi."));
        }
    } else if (finalPass) {
        m_busy = false;
        emit finished(text);
    } else if (m_holding) {
        emit partial(text);
    }
    kickFinal();
}

} // namespace zappe
