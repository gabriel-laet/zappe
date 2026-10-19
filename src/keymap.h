#pragma once

#include <QString>
#include <QVector>

namespace zappe {

// Contrato do controle ATONGX na sala. Power 116 alterna a Samsung.
// O nó dessa tecla continua com EVIOCGRAB: o logind não desliga o mini-PC.
// O botão Home da sala sai como KEY_BACK 158. KEY_HOMEPAGE 172 e KEY_HOME 102
// também são Home: voltam para a grade e focam o primeiro card.
// Del 14 e Escape 1 continuam voltando um nível.
enum class RemoteAction {
    Unmapped = 0,
    Ignore,
    Up,
    Down,
    Left,
    Right,
    Ok,
    Back,
    Home,
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
// 0–9, 10 para o ponto do canal virtual (7.1). -1 se a tecla não é número.
int dialFromEvdev(int code);
const char *actionName(RemoteAction action);
QString describeEvdevKey(int code);
QString describeX11Key(int scan, int qtKey);
// Nome da ação para o log, ou vazio se a tecla Qt não entra no contrato.
QString actionNameForQt(int scan, int qtKey);

struct MapBinding {
    int code = 0;
    QString action;
    QString name;
};

// Lê data/atongx-map.json (arquivo ou recurso) para o capturador e o self-check.
QVector<MapBinding> loadMapBindings(const QByteArray &json, QString *error);

} // namespace zappe
