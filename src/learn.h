#pragma once

#include "mirror.h"

#include <QByteArray>
#include <QString>
#include <QVector>

namespace zappe {

struct LlmConfig {
    QString url;
    QString key;
    QString model;
};

LlmConfig loadLlmConfig(const QString &dataDir);
bool llmEnabled(const LlmConfig &config);
QByteArray llmAuthorization(const LlmConfig &config);
QByteArray llmRepairBody(const MirrorAdapter &current, const QString &dom, const QVector<NetworkSample> &samples,
                         const QByteArray &screenshot, const QString &model);
bool validateLearnedAdapter(const MirrorAdapter &candidate, const QVector<NetworkSample> &samples, int minTitles);
bool saveLearnedAdapter(const QString &dataDir, MirrorAdapter &adapter);
bool rollbackLearnedAdapter(const QString &dataDir, const QString &serviceId);
bool applyLearnResponse(const QString &dataDir, const MirrorAdapter &base, const QVector<NetworkSample> &samples,
                        const QByteArray &response);

} // namespace zappe
