#pragma once

#include <QObject>

class QTimer;

namespace zappe {

// Enquanto o canal está no ar, o mpv fica com o foco e o xdotool manda o
// keycode 166 (XF86Back) para ele. O mpv não conhece o nome XF86Back. Este
// grab no X entrega a tecla ao app, que fecha o processo.
class XBackGrab : public QObject {
    Q_OBJECT
public:
    explicit XBackGrab(QObject *parent = nullptr);
    ~XBackGrab() override;

    void setActive(bool active);

signals:
    void pressed();

private:
    void poll();
    void shutdown();

    struct Priv;
    Priv *m_priv;
    QTimer *m_timer = nullptr;
};

} // namespace zappe
