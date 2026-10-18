#pragma once

#include <QByteArray>
#include <QString>

namespace zappe {

// Agora / a seguir lidos do TS (EIT present/following e, se faltar, a grade).
struct EpgSnapshot {
    QString nowTitle;
    QString nextTitle;
    QString serviceName;
    int serviceId = 0;
};

QString epgGuideLine(const QString &nowTitle, const QString &nextTitle);

// Monta uma seção EIT (0x4E presente/seguinte, ou 0x50–0x5F grade) com um short_event.
QByteArray buildEitSection(int tableId, int sectionNumber, int serviceId, const QString &title);
// SDT (0x42) com o service_descriptor do nome.
QByteArray buildSdtSection(int serviceId, const QString &name);
// Empacota a seção em pacotes TS de 188 bytes no PID pedido.
QByteArray wrapTsSection(const QByteArray &section, int pid);

EpgSnapshot parseEitSection(const QByteArray &section);
EpgSnapshot parseEpgFromTs(const QByteArray &ts);

} // namespace zappe
