#pragma once

#include <QString>
#include <QVector>

namespace zappe {

// Contrato do controle ATONGX na sala. Power 116 alterna a Samsung.
// O nó dessa tecla continua com EVIOCGRAB: o logind não desliga o mini-PC.
// Home do controle chega como KEY_BACK 158 e significa voltar.
enum class RemoteAction {
    Unmapped = 0,
    Ignore,
    Up,
    Down,
    Left,
    Right,
    Ok,
    Back,
    Menu,
    PlayPause,
    PageUp,
    PageDown,
    VolumeUp,
    VolumeDown,
    Mute,
    Voice,
    Power,
};

RemoteAction actionForEvdev(int code);
const char *actionName(RemoteAction action);

struct MapBinding {
    int code = 0;
    QString action;
    QString name;
};

// Lê data/atongx-map.json (arquivo ou recurso) para o capturador e o self-check.
QVector<MapBinding> loadMapBindings(const QByteArray &json, QString *error);

} // namespace zappe
