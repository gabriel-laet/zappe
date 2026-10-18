#pragma once

#include <QString>
#include <QUrl>

inline quint32 fnv1a(const QString &text)
{
    quint32 hash = 2166136261u;
    const QByteArray bytes = text.toUtf8();
    for (unsigned char c : bytes) {
        hash ^= c;
        hash *= 16777619u;
    }
    return hash;
}

inline QString fileUrl(const QString &path)
{
    if (path.isEmpty())
        return {};
    return QUrl::fromLocalFile(path).toString();
}
