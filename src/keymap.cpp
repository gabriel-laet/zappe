#include "keymap.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace zappe {

RemoteAction actionForEvdev(int code)
{
    switch (code) {
    case 116:
        return RemoteAction::Power;
    case 164:
        return RemoteAction::PlayPause;
    case 103:
        return RemoteAction::Up;
    case 108:
        return RemoteAction::Down;
    case 105:
        return RemoteAction::Left;
    case 106:
        return RemoteAction::Right;
    case 28:
        return RemoteAction::Ok;
    case 158:
    case 14:
    case 1:
        return RemoteAction::Back;
    case 172:
    case 102:
        return RemoteAction::Home;
    case 127:
        return RemoteAction::Menu;
    case 104:
        return RemoteAction::PageUp;
    case 109:
        return RemoteAction::PageDown;
    case 115:
        return RemoteAction::VolumeUp;
    case 114:
        return RemoteAction::VolumeDown;
    case 113:
        return RemoteAction::Mute;
    case 582:
        return RemoteAction::Voice;
    default:
        return RemoteAction::Unmapped;
    }
}

const char *actionName(RemoteAction action)
{
    switch (action) {
    case RemoteAction::Ignore:
        return "ignore";
    case RemoteAction::Up:
        return "up";
    case RemoteAction::Down:
        return "down";
    case RemoteAction::Left:
        return "left";
    case RemoteAction::Right:
        return "right";
    case RemoteAction::Ok:
        return "ok";
    case RemoteAction::Back:
        return "back";
    case RemoteAction::Home:
        return "home";
    case RemoteAction::Menu:
        return "menu";
    case RemoteAction::PlayPause:
        return "playpause";
    case RemoteAction::PageUp:
        return "pageUp";
    case RemoteAction::PageDown:
        return "pageDown";
    case RemoteAction::VolumeUp:
        return "volumeUp";
    case RemoteAction::VolumeDown:
        return "volumeDown";
    case RemoteAction::Mute:
        return "mute";
    case RemoteAction::Voice:
        return "voice";
    case RemoteAction::Power:
        return "power";
    case RemoteAction::Unmapped:
        break;
    }
    return "unmapped";
}

QVector<MapBinding> loadMapBindings(const QByteArray &json, QString *error)
{
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = parse.errorString();
        return {};
    }
    QVector<MapBinding> out;
    const QJsonArray bindings = doc.object().value(QStringLiteral("bindings")).toArray();
    for (const QJsonValue &bindingValue : bindings) {
        const QJsonObject binding = bindingValue.toObject();
        const QString action = binding.value(QStringLiteral("action")).toString();
        const QJsonArray keys = binding.value(QStringLiteral("keys")).toArray();
        for (const QJsonValue &keyValue : keys) {
            const QJsonObject key = keyValue.toObject();
            MapBinding row;
            row.code = key.value(QStringLiteral("code")).toInt(-1);
            row.action = action;
            row.name = key.value(QStringLiteral("name")).toString();
            if (row.code >= 0 && !row.action.isEmpty())
                out.append(row);
        }
    }
    return out;
}

} // namespace zappe
