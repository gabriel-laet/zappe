#include "epg.h"

#include <QHash>
#include <QStringList>

#include <cstring>

namespace zappe {
namespace {

int u8(const QByteArray &data, int index)
{
    if (index < 0 || index >= data.size())
        return 0;
    return static_cast<unsigned char>(data.at(index));
}

void append16(QByteArray &out, int value)
{
    out.append(char((value >> 8) & 0xff));
    out.append(char(value & 0xff));
}

QString latinText(const QByteArray &raw)
{
    QByteArray text = raw;
    if (!text.isEmpty() && static_cast<unsigned char>(text.at(0)) < 0x20)
        text.remove(0, 1);
    return QString::fromLatin1(text).trimmed();
}

QString shortEventTitle(const QByteArray &data, int begin, int end)
{
    int i = begin;
    while (i + 2 <= end) {
        const int tag = u8(data, i);
        const int len = u8(data, i + 1);
        i += 2;
        if (i + len > end)
            break;
        if (tag == 0x4D && len >= 4) {
            const int nameLen = u8(data, i + 3);
            if (nameLen >= 0 && i + 4 + nameLen <= i + len) {
                const QString title = latinText(data.mid(i + 4, nameLen));
                if (!title.isEmpty())
                    return title;
            }
        }
        i += len;
    }
    return {};
}

QString serviceDescriptorName(const QByteArray &data, int begin, int end)
{
    int i = begin;
    while (i + 2 <= end) {
        const int tag = u8(data, i);
        const int len = u8(data, i + 1);
        i += 2;
        if (i + len > end)
            break;
        if (tag == 0x48 && len >= 2) {
            const int providerLen = u8(data, i + 1);
            const int nameAt = i + 2 + providerLen;
            if (nameAt < i + len) {
                const int nameLen = u8(data, nameAt);
                if (nameLen > 0 && nameAt + 1 + nameLen <= i + len)
                    return latinText(data.mid(nameAt + 1, nameLen));
            }
        }
        i += len;
    }
    return {};
}

bool isEitTable(int tableId)
{
    return tableId == 0x4E || tableId == 0x4F || (tableId >= 0x50 && tableId <= 0x5F);
}

void takeSection(EpgSnapshot &snap, QStringList &schedule, QHash<int, QString> &names, const QByteArray &section)
{
    if (section.size() < 3)
        return;
    const int tableId = u8(section, 0);
    const int sectionLength = ((u8(section, 1) & 0x0f) << 8) | u8(section, 2);
    const int total = 3 + sectionLength;
    if (sectionLength < 4 || section.size() < total)
        return;
    const int end = total - 4;

    if (tableId == 0x42 && section.size() >= 11) {
        int i = 11;
        while (i + 5 <= end) {
            const int serviceId = (u8(section, i) << 8) | u8(section, i + 1);
            const int descLen = ((u8(section, i + 3) & 0x0f) << 8) | u8(section, i + 4);
            i += 5;
            if (i + descLen > end)
                break;
            const QString name = serviceDescriptorName(section, i, i + descLen);
            if (!name.isEmpty())
                names.insert(serviceId, name);
            i += descLen;
        }
        return;
    }

    if (!isEitTable(tableId) || section.size() < 14)
        return;
    const int serviceId = (u8(section, 3) << 8) | u8(section, 4);
    const int sectionNumber = u8(section, 6);
    int i = 14;
    while (i + 12 <= end) {
        const int descLen = ((u8(section, i + 10) & 0x0f) << 8) | u8(section, i + 11);
        const int descAt = i + 12;
        if (descAt + descLen > end)
            break;
        const QString title = shortEventTitle(section, descAt, descAt + descLen);
        const bool present = tableId == 0x4E || tableId == 0x4F;
        if (present && sectionNumber == 0 && snap.nowTitle.isEmpty() && !title.isEmpty()) {
            snap.nowTitle = title;
            snap.serviceId = serviceId;
        } else if (present && sectionNumber == 1 && snap.nextTitle.isEmpty() && !title.isEmpty()) {
            snap.nextTitle = title;
            if (snap.serviceId == 0)
                snap.serviceId = serviceId;
        } else if (!present && !title.isEmpty()) {
            schedule.append(title);
            if (snap.serviceId == 0)
                snap.serviceId = serviceId;
        }
        i = descAt + descLen;
    }
}

} // namespace

QString epgGuideLine(const QString &nowTitle, const QString &nextTitle)
{
    const QString now = nowTitle.trimmed();
    const QString next = nextTitle.trimmed();
    if (now.isEmpty() && next.isEmpty())
        return {};
    if (next.isEmpty())
        return QStringLiteral("Agora: %1").arg(now);
    if (now.isEmpty())
        return QStringLiteral("A seguir: %1").arg(next);
    return QStringLiteral("Agora: %1 · A seguir: %2").arg(now, next);
}

QByteArray buildEitSection(int tableId, int sectionNumber, int serviceId, const QString &title)
{
    const QByteArray name = title.toLatin1();
    QByteArray body;
    body.append('p');
    body.append('o');
    body.append('r');
    body.append(char(name.size()));
    body.append(name);
    body.append(char(0));
    QByteArray desc;
    desc.append(char(0x4D));
    desc.append(char(body.size()));
    desc.append(body);

    QByteArray event;
    append16(event, 1);
    event.append(QByteArray(5, '\0'));
    event.append(QByteArray(3, '\0'));
    const int descLen = desc.size();
    event.append(char((descLen >> 8) & 0x0f));
    event.append(char(descLen & 0xff));
    event.append(desc);

    QByteArray after;
    append16(after, serviceId);
    after.append(char(0xC1));
    after.append(char(sectionNumber & 0xff));
    after.append(char(sectionNumber & 0xff));
    append16(after, 1);
    append16(after, 1);
    after.append(char(sectionNumber & 0xff));
    after.append(char(tableId & 0xff));
    after.append(event);
    after.append(QByteArray(4, '\0'));

    QByteArray section;
    section.append(char(tableId & 0xff));
    section.append(char(0xB0 | ((after.size() >> 8) & 0x0f)));
    section.append(char(after.size() & 0xff));
    section.append(after);
    return section;
}

QByteArray buildSdtSection(int serviceId, const QString &name)
{
    const QByteArray raw = name.toLatin1();
    QByteArray body;
    body.append(char(0x01));
    body.append(char(0));
    body.append(char(raw.size()));
    body.append(raw);
    QByteArray desc;
    desc.append(char(0x48));
    desc.append(char(body.size()));
    desc.append(body);

    QByteArray service;
    append16(service, serviceId);
    service.append(char(0xFC));
    const int descLen = desc.size();
    service.append(char(0x80 | ((descLen >> 8) & 0x0f)));
    service.append(char(descLen & 0xff));
    service.append(desc);

    QByteArray after;
    append16(after, 1);
    after.append(char(0xC1));
    after.append(char(0));
    after.append(char(0));
    append16(after, 1);
    after.append(char(0xFF));
    after.append(service);
    after.append(QByteArray(4, '\0'));

    QByteArray section;
    section.append(char(0x42));
    section.append(char(0xB0 | ((after.size() >> 8) & 0x0f)));
    section.append(char(after.size() & 0xff));
    section.append(after);
    return section;
}

QByteArray wrapTsSection(const QByteArray &section, int pid)
{
    QByteArray out;
    int offset = 0;
    int continuity = 0;
    while (offset < section.size()) {
        QByteArray packet(188, char(0xFF));
        packet[0] = 0x47;
        const bool start = offset == 0;
        packet[1] = char((start ? 0x40 : 0) | ((pid >> 8) & 0x1f));
        packet[2] = char(pid & 0xff);
        packet[3] = char(0x10 | (continuity & 0x0f));
        continuity = (continuity + 1) & 0x0f;
        int pos = 4;
        if (start) {
            packet[4] = 0;
            pos = 5;
        }
        const int space = 188 - pos;
        const int count = qMin(space, section.size() - offset);
        memcpy(packet.data() + pos, section.constData() + offset, size_t(count));
        offset += count;
        out.append(packet);
    }
    return out;
}

EpgSnapshot parseEitSection(const QByteArray &section)
{
    EpgSnapshot snap;
    QStringList schedule;
    QHash<int, QString> names;
    takeSection(snap, schedule, names, section);
    if (snap.nowTitle.isEmpty()) {
        for (const QString &title : schedule) {
            snap.nowTitle = title;
            break;
        }
    }
    if (snap.nextTitle.isEmpty()) {
        for (const QString &title : schedule) {
            if (title != snap.nowTitle) {
                snap.nextTitle = title;
                break;
            }
        }
    }
    if (snap.serviceId != 0)
        snap.serviceName = names.value(snap.serviceId);
    return snap;
}

EpgSnapshot parseEpgFromTs(const QByteArray &ts)
{
    EpgSnapshot snap;
    QStringList schedule;
    QHash<int, QString> names;
    QHash<int, QByteArray> partial;
    const int packets = ts.size() / 188;
    for (int n = 0; n < packets; ++n) {
        const int at = n * 188;
        if (u8(ts, at) != 0x47)
            continue;
        const int pid = ((u8(ts, at + 1) & 0x1f) << 8) | u8(ts, at + 2);
        if (pid != 0x11 && pid != 0x12)
            continue;
        const int adaptation = (u8(ts, at + 3) >> 4) & 0x03;
        if (adaptation == 0 || adaptation == 2)
            continue;
        int pos = at + 4;
        if (adaptation == 3) {
            const int adaptLen = u8(ts, pos);
            pos += 1 + adaptLen;
            if (pos >= at + 188)
                continue;
        }
        const bool start = (u8(ts, at + 1) & 0x40) != 0;
        if (start) {
            const int pointer = u8(ts, pos);
            pos += 1 + pointer;
            if (!partial.value(pid).isEmpty())
                takeSection(snap, schedule, names, partial.take(pid));
        }
        if (pos >= at + 188)
            continue;
        QByteArray &buf = partial[pid];
        if (!start && buf.isEmpty())
            continue;
        buf.append(ts.mid(pos, at + 188 - pos));
        while (buf.size() >= 3 && u8(buf, 0) != 0xFF) {
            const int sectionLength = ((u8(buf, 1) & 0x0f) << 8) | u8(buf, 2);
            const int total = 3 + sectionLength;
            if (sectionLength < 4 || buf.size() < total)
                break;
            takeSection(snap, schedule, names, buf.left(total));
            buf.remove(0, total);
        }
        if (!buf.isEmpty() && u8(buf, 0) == 0xFF)
            buf.clear();
    }
    for (auto it = partial.cbegin(); it != partial.cend(); ++it) {
        if (it.value().size() >= 3)
            takeSection(snap, schedule, names, it.value());
    }
    if (snap.nowTitle.isEmpty()) {
        for (const QString &title : schedule) {
            snap.nowTitle = title;
            break;
        }
    }
    if (snap.nextTitle.isEmpty()) {
        for (const QString &title : schedule) {
            if (title != snap.nowTitle) {
                snap.nextTitle = title;
                break;
            }
        }
    }
    if (snap.serviceName.isEmpty() && snap.serviceId != 0)
        snap.serviceName = names.value(snap.serviceId);
    if (snap.serviceName.isEmpty() && names.size() == 1)
        snap.serviceName = names.constBegin().value();
    return snap;
}

} // namespace zappe
