#include "keymap.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>

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
    case 172:
    case 102:
        return RemoteAction::Home;
    case 158:
    case 14:
    case 1:
        return RemoteAction::Back;
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

int dialFromEvdev(int code)
{
    // KEY_1..KEY_9 = 2..10, KEY_0 = 11. Numérico: 1=79, 2=80, 3=81, 4=75, 5=76, 6=77, 7=71, 8=72, 9=73, 0=82.
    switch (code) {
    case 11:
    case 82:
        return 0;
    case 2:
    case 79:
        return 1;
    case 3:
    case 80:
        return 2;
    case 4:
    case 81:
        return 3;
    case 5:
    case 75:
        return 4;
    case 6:
    case 76:
        return 5;
    case 7:
    case 77:
        return 6;
    case 8:
    case 71:
        return 7;
    case 9:
    case 72:
        return 8;
    case 10:
    case 73:
        return 9;
    case 52:
    case 83:
        return 10;
    default:
        return -1;
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

QString describeEvdevKey(int code)
{
    switch (code) {
    case 1:
        return QStringLiteral("KEY_ESC");
    case 14:
        return QStringLiteral("KEY_BACKSPACE");
    case 28:
        return QStringLiteral("KEY_ENTER");
    case 102:
        return QStringLiteral("KEY_HOME");
    case 103:
        return QStringLiteral("KEY_UP");
    case 104:
        return QStringLiteral("KEY_PAGEUP");
    case 105:
        return QStringLiteral("KEY_LEFT");
    case 106:
        return QStringLiteral("KEY_RIGHT");
    case 108:
        return QStringLiteral("KEY_DOWN");
    case 109:
        return QStringLiteral("KEY_PAGEDOWN");
    case 113:
        return QStringLiteral("KEY_MUTE");
    case 114:
        return QStringLiteral("KEY_VOLUMEDOWN");
    case 115:
        return QStringLiteral("KEY_VOLUMEUP");
    case 116:
        return QStringLiteral("KEY_POWER");
    case 127:
        return QStringLiteral("KEY_MENU");
    case 158:
        return QStringLiteral("KEY_BACK");
    case 164:
        return QStringLiteral("KEY_PLAYPAUSE");
    case 172:
        return QStringLiteral("KEY_HOMEPAGE");
    case 582:
        return QStringLiteral("KEY_VOICE");
    default:
        break;
    }
    const int dial = dialFromEvdev(code);
    if (dial == 10)
        return QStringLiteral("KEY_DOT");
    if (dial >= 0)
        return QStringLiteral("KEY_%1").arg(dial);
    return QStringLiteral("KEY_%1").arg(code);
}

QString describeX11Key(int scan, int qtKey)
{
    if (scan == 166)
        return QStringLiteral("XF86Back");
    switch (qtKey) {
    case Qt::Key_Escape:
        return QStringLiteral("Escape");
    case Qt::Key_Backspace:
        return QStringLiteral("Backspace");
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return QStringLiteral("Return");
    case Qt::Key_Select:
        return QStringLiteral("Select");
    case Qt::Key_Left:
        return QStringLiteral("Left");
    case Qt::Key_Right:
        return QStringLiteral("Right");
    case Qt::Key_Up:
        return QStringLiteral("Up");
    case Qt::Key_Down:
        return QStringLiteral("Down");
    case Qt::Key_Back:
        return QStringLiteral("Back");
    case Qt::Key_Home:
        return QStringLiteral("Home");
    case Qt::Key_HomePage:
        return QStringLiteral("HomePage");
    case Qt::Key_Menu:
        return QStringLiteral("Menu");
    case Qt::Key_VolumeUp:
        return QStringLiteral("VolumeUp");
    case Qt::Key_VolumeDown:
        return QStringLiteral("VolumeDown");
    case Qt::Key_VolumeMute:
        return QStringLiteral("VolumeMute");
    case Qt::Key_PageUp:
        return QStringLiteral("PageUp");
    case Qt::Key_PageDown:
        return QStringLiteral("PageDown");
    default:
        break;
    }
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9)
        return QStringLiteral("KEY_%1").arg(qtKey - Qt::Key_0);
    return QStringLiteral("Qt_%1").arg(qtKey);
}

QString actionNameForQt(int scan, int qtKey)
{
    if (qtKey == Qt::Key_Home || qtKey == Qt::Key_HomePage)
        return QStringLiteral("home");
    if (scan == 166 || qtKey == Qt::Key_Back)
        return QStringLiteral("back");
    switch (qtKey) {
    case Qt::Key_Escape:
    case Qt::Key_Backspace:
        return QStringLiteral("back");
    case Qt::Key_Left:
        return QStringLiteral("left");
    case Qt::Key_Right:
        return QStringLiteral("right");
    case Qt::Key_Up:
        return QStringLiteral("up");
    case Qt::Key_Down:
        return QStringLiteral("down");
    case Qt::Key_PageUp:
        return QStringLiteral("pageUp");
    case Qt::Key_PageDown:
        return QStringLiteral("pageDown");
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Select:
        return QStringLiteral("ok");
    case Qt::Key_Menu:
        return QStringLiteral("menu");
    case Qt::Key_VolumeUp:
        return QStringLiteral("volumeUp");
    case Qt::Key_VolumeDown:
        return QStringLiteral("volumeDown");
    case Qt::Key_VolumeMute:
        return QStringLiteral("mute");
    default:
        break;
    }
    return {};
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
