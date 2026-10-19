#pragma once

#include <QObject>

namespace zappe {

// Lê o dongle ATONGX / XING WEI. O nó event4 (air mouse) é capturado
// com EVIOCGRAB e os eventos são descartados para o cursor não se mover.
class RemoteWatcher : public QObject {
    Q_OBJECT
public:
    explicit RemoteWatcher(QObject *parent = nullptr);
    ~RemoteWatcher() override;
    void start();
    // Enquanto o Chrome está na frente, o nó do teclado e qualquer nó com
    // KEY_BACK ficam exclusivos. O Home não chega no navegador (senão a
    // kiosk cai em about:blank) e as setas são reenviadas por CDP.
    void setHoldKeyboard(bool hold);

signals:
    void action(int remoteAction);
    // Todo EV_KEY com value 1, inclusive número e tecla sem ação.
    void keyCode(int code);
    // 0–9 ou 10 para o ponto. O nó exclusivo ainda entrega o número.
    void dialKey(int value);

private:
    class Worker;
    Worker *m_worker = nullptr;
    bool m_holdKeyboard = false;
};

bool hidDisabled();

// O air mouse (event4) e o nó que só tem KEY_POWER são capturados.
// Sem esse grab o logind vê a tecla 116 e pode desligar o mini-PC.
bool shouldGrabEvdevNode(bool airMouse, bool hasKeyPower, bool hasKeyUp);

} // namespace zappe
