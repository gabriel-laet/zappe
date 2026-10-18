#include "voice.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>

#include <cstdio>

namespace zappe {
namespace {

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

VoiceIntent parseVoicePtBr(const QString &text)
{
    const QString folded = foldVoice(text);
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

QString whisperModel(const QString &dataDir)
{
    if (const QString explicitPath = qEnvironmentVariable("ZAPPE_WHISPER_MODEL"); !explicitPath.isEmpty()) {
        if (QFile::exists(explicitPath))
            return explicitPath;
    }
    const QString dir = QDir(dataDir).filePath(QStringLiteral("whisper"));
    const QStringList names = {QStringLiteral("ggml-base.bin"), QStringLiteral("ggml-tiny.bin"),
                               QStringLiteral("ggml-base-q5_1.bin"), QStringLiteral("ggml-tiny-q5_1.bin"),
                               QStringLiteral("ggml-small.bin")};
    for (const QString &name : names) {
        const QString path = QDir(dir).filePath(name);
        if (QFile::exists(path))
            return path;
    }
    return {};
}

VoiceSession::VoiceSession(QObject *parent)
    : QObject(parent)
{
}

void VoiceSession::cancel()
{
    m_busy = false;
}

void VoiceSession::listen(const QString &dataDir)
{
    if (m_busy)
        return;
    if (const QString fake = qEnvironmentVariable("ZAPPE_VOICE_FAKE"); !fake.isEmpty()) {
        emit started();
        emit finished(fake);
        return;
    }
    m_busy = true;
    emit started();

    const QString wav = QDir(QDir::tempPath()).filePath(QStringLiteral("zappe-voice.wav"));
    int seconds = qEnvironmentVariableIntValue("ZAPPE_VOICE_SECONDS");
    if (seconds < 2 || seconds > 8)
        seconds = 5;
    QStringList args = {QStringLiteral("-q"), QStringLiteral("-d"), QString::number(seconds),
                        QStringLiteral("-f"), QStringLiteral("S16_LE"), QStringLiteral("-r"),
                        QStringLiteral("16000"), QStringLiteral("-c"), QStringLiteral("1")};
    if (const QString device = qEnvironmentVariable("ZAPPE_ALSA_DEVICE"); !device.isEmpty())
        args << QStringLiteral("-D") << device;
    args << wav;

    auto *record = new QProcess(this);
    connect(record, &QProcess::finished, this, [this, record, wav, dataDir](int code, QProcess::ExitStatus status) {
        record->deleteLater();
        if (!m_busy)
            return;
        if (status != QProcess::NormalExit || code != 0 || !QFile::exists(wav)) {
            m_busy = false;
            emit failed(QStringLiteral("Microfone indisponível (arecord)."));
            return;
        }
        transcribe(wav);
        Q_UNUSED(dataDir);
    });
    record->start(QStringLiteral("arecord"), args);
    if (!record->waitForStarted(1500)) {
        record->deleteLater();
        m_busy = false;
        emit failed(QStringLiteral("arecord não iniciou."));
    }
}

void VoiceSession::transcribe(const QString &wav)
{
    const QString dataDir = qEnvironmentVariable("ZAPPE_DATA_DIR");
    const QString home = QDir::homePath() + QStringLiteral("/.local/share/zappe");
    const QString model = whisperModel(dataDir.isEmpty() ? home : dataDir);
    if (model.isEmpty()) {
        m_busy = false;
        emit failed(QStringLiteral("Modelo Whisper base/tiny não instalado."));
        return;
    }
    QString bin = qEnvironmentVariable("ZAPPE_WHISPER_BIN");
    if (bin.isEmpty())
        bin = QStringLiteral("whisper-cli");
    auto *whisper = new QProcess(this);
    connect(whisper, &QProcess::finished, this, [this, whisper](int code, QProcess::ExitStatus status) {
        const QString raw = QString::fromUtf8(whisper->readAllStandardOutput())
                            + QString::fromUtf8(whisper->readAllStandardError());
        whisper->deleteLater();
        m_busy = false;
        if (status != QProcess::NormalExit || code != 0) {
            emit failed(QStringLiteral("Whisper falhou."));
            return;
        }
        QStringList lines;
        for (const QString &line : raw.split(QLatin1Char('\n'))) {
            QString cleaned = line.trimmed();
            if (cleaned.startsWith(QLatin1Char('['))) {
                const int end = cleaned.indexOf(QLatin1Char(']'));
                if (end >= 0)
                    cleaned = cleaned.mid(end + 1).trimmed();
            }
            if (!cleaned.isEmpty() && !cleaned.contains(QStringLiteral("whisper.cpp"), Qt::CaseInsensitive))
                lines.append(cleaned);
        }
        emit finished(lines.join(QLatin1Char(' ')).trimmed());
    });
    whisper->start(bin, {QStringLiteral("-m"), model, QStringLiteral("-f"), wav, QStringLiteral("-l"),
                         QStringLiteral("pt"), QStringLiteral("-nt"), QStringLiteral("--prompt"),
                         QStringLiteral("Abrir Netflix. Voltar. Volume mais. Mudo. Ir para Globo.")});
    if (!whisper->waitForStarted(1500)) {
        whisper->deleteLater();
        m_busy = false;
        emit failed(QStringLiteral("whisper-cli não iniciou."));
    }
}

} // namespace zappe
