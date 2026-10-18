#pragma once

#include "catalog.h"

#include <QDateTime>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QVector>

class QLocalSocket;
class QTimer;

namespace zappe {

struct OtaChannel {
    QString name;
    QString source;
};

QStringList parseChannelNames(const QString &text);
QStringList filterAndSortChannels(QStringList names);
QString resolveChannelName(const QString &requested, const QStringList &names);
QString otaPipelineScript(const QString &conf, const QString &channel);
bool mpvPlaybackStarted(const QByteArray &ipcLine);

QVector<OtaChannel> listOtaChannels();
void mergeLiveChannels(CatalogFile &catalog, const QVector<OtaChannel> &channels);
QString otaFramePath(const QString &dataDir, const QString &channel);
bool otaFrameIsFresh(const QString &path, const QDateTime &now);
int otaFrameIntervalMs();
void assignOtaFrame(RowData &row, const QString &dataDir, const QDateTime &now);

class OtaPlayer : public QObject {
    Q_OBJECT
public:
    explicit OtaPlayer(QObject *parent = nullptr);
    bool playing() const { return m_process != nullptr; }
    void play(const QString &requested);
    void stop();
    void togglePause();

signals:
    void started(const QString &channel);
    void failed(const QString &message);
    void stopped();
    void frameReady(const QString &channel, const QString &path);

private:
    void pollPicture();
    void armFrames();
    void captureFrame();

    QProcess *m_process = nullptr;
    QTimer *m_pictureTimer = nullptr;
    QTimer *m_frameTimer = nullptr;
    QLocalSocket *m_ipc = nullptr;
    QString m_channel;
    QString m_socket;
    bool m_userStop = false;
    bool m_sawPicture = false;
};

} // namespace zappe
