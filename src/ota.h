#pragma once

#include "catalog.h"

#include <QObject>
#include <QProcess>
#include <QString>
#include <QVector>

namespace zappe {

struct OtaChannel {
    QString name;
    QString source;
};

QStringList parseChannelNames(const QString &text);
QStringList filterAndSortChannels(QStringList names);
QString resolveChannelName(const QString &requested, const QStringList &names);
QString otaPipelineScript(const QString &conf, const QString &channel);

QVector<OtaChannel> listOtaChannels();
void mergeLiveChannels(CatalogFile &catalog, const QVector<OtaChannel> &channels);

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

private:
    QProcess *m_process = nullptr;
    QString m_channel;
    QString m_socket;
};

} // namespace zappe
