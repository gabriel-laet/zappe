#pragma once

#include "mirror.h"

#include <QByteArray>
#include <QDate>
#include <QString>
#include <QVector>

namespace zappe {

struct LlmConfig {
    QString url;
    QString key;
    QString model;
    QString fallbackModel;
    QString sessionHeader;
    QString userAgent;
};

struct LlmHeader {
    QByteArray name;
    QByteArray value;
};

LlmConfig loadLlmConfig(const QString &dataDir);
bool llmEnabled(const LlmConfig &config);
QByteArray llmAuthorization(const LlmConfig &config);
// zappe/<versão> quando userAgent está vazio.
QString llmUserAgent(const LlmConfig &config);
// zappe-<serviço>-<yyyymmdd>. O mesmo serviço no mesmo dia reusa o id.
QString llmSessionId(const QString &serviceId, const QDate &day);
// Content-Type, User-Agent, Authorization e, se sessionHeader estiver preenchido, esse cabeçalho.
QVector<LlmHeader> llmRequestHeaders(const LlmConfig &config, const QString &serviceId, const QDate &day);
// Uma vez, quando o pedido falha ou o adaptador não valida e há um modelo diferente.
bool llmUseFallback(bool requestFailed, bool adapterRejected, bool alreadyTriedFallback, const QString &fallbackModel,
                    const QString &usedModel);
// model, status e latência. Não recebe a chave.
QString llmResultLog(const QString &serviceId, const QString &model, int httpStatus, qint64 elapsedMs);
QByteArray llmRepairBody(const MirrorAdapter &current, const QString &dom, const QVector<NetworkSample> &samples,
                         const QByteArray &screenshot, const QString &model);
bool validateLearnedAdapter(const MirrorAdapter &candidate, const QVector<NetworkSample> &samples, int minTitles);
bool saveLearnedAdapter(const QString &dataDir, MirrorAdapter &adapter);
bool rollbackLearnedAdapter(const QString &dataDir, const QString &serviceId);
bool applyLearnResponse(const QString &dataDir, const MirrorAdapter &base, const QVector<NetworkSample> &samples,
                        const QByteArray &response);

} // namespace zappe
