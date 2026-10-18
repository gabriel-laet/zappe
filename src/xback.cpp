#include "xback.h"

#include <QTimer>

#include <X11/XF86keysym.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <cstdio>

namespace zappe {
namespace {

int g_grabError = 0;

int grabErrorHandler(Display *, XErrorEvent *event)
{
    g_grabError = event->error_code;
    return 0;
}

} // namespace

struct XBackGrab::Priv {
    Display *dpy = nullptr;
    int codes[2] = {0, 0};
    int ncodes = 0;
    bool active = false;
    bool openFailed = false;
};

XBackGrab::XBackGrab(QObject *parent)
    : QObject(parent)
    , m_priv(new Priv)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(30);
    connect(m_timer, &QTimer::timeout, this, &XBackGrab::poll);
}

XBackGrab::~XBackGrab()
{
    shutdown();
    delete m_priv;
}

void XBackGrab::shutdown()
{
    if (!m_priv || !m_priv->dpy)
        return;
    if (m_timer)
        m_timer->stop();
    if (m_priv->active) {
        const Window root = DefaultRootWindow(m_priv->dpy);
        for (int i = 0; i < m_priv->ncodes; ++i)
            XUngrabKey(m_priv->dpy, m_priv->codes[i], AnyModifier, root);
        XFlush(m_priv->dpy);
        m_priv->active = false;
    }
    XCloseDisplay(m_priv->dpy);
    m_priv->dpy = nullptr;
}

void XBackGrab::setActive(bool active)
{
    if (!m_priv || m_priv->openFailed || active == m_priv->active)
        return;
    if (!m_priv->dpy) {
        m_priv->dpy = XOpenDisplay(nullptr);
        if (!m_priv->dpy) {
            m_priv->openFailed = true;
            std::fprintf(stderr, "zappe: sem DISPLAY para capturar XF86Back\n");
            return;
        }
        const int fromSym = int(XKeysymToKeycode(m_priv->dpy, XF86XK_Back));
        auto add = [this](int code) {
            if (code <= 0 || m_priv->ncodes >= 2)
                return;
            for (int i = 0; i < m_priv->ncodes; ++i) {
                if (m_priv->codes[i] == code)
                    return;
            }
            m_priv->codes[m_priv->ncodes++] = code;
        };
        add(fromSym);
        add(166);
        if (m_priv->ncodes == 0) {
            m_priv->openFailed = true;
            std::fprintf(stderr, "zappe: XF86Back sem keycode neste X\n");
            return;
        }
    }
    const Window root = DefaultRootWindow(m_priv->dpy);
    const XErrorHandler previous = XSetErrorHandler(grabErrorHandler);
    g_grabError = 0;
    for (int i = 0; i < m_priv->ncodes; ++i) {
        if (active)
            XGrabKey(m_priv->dpy, m_priv->codes[i], AnyModifier, root, False, GrabModeAsync, GrabModeAsync);
        else
            XUngrabKey(m_priv->dpy, m_priv->codes[i], AnyModifier, root);
    }
    XSync(m_priv->dpy, False);
    XSetErrorHandler(previous);
    if (active && g_grabError != 0) {
        std::fprintf(stderr, "zappe: XF86Back no X não foi capturado (erro %d)\n", g_grabError);
        for (int i = 0; i < m_priv->ncodes; ++i)
            XUngrabKey(m_priv->dpy, m_priv->codes[i], AnyModifier, root);
        XFlush(m_priv->dpy);
        return;
    }
    m_priv->active = active;
    if (active) {
        std::fprintf(stderr, "zappe: XF86Back no X, keycode %d, exclusivo com o canal no ar\n", m_priv->codes[0]);
        m_timer->start();
        poll();
    } else {
        m_timer->stop();
    }
}

void XBackGrab::poll()
{
    if (!m_priv || !m_priv->dpy || !m_priv->active)
        return;
    while (XPending(m_priv->dpy)) {
        XEvent event;
        XNextEvent(m_priv->dpy, &event);
        if (event.type != KeyPress)
            continue;
        const int code = event.xkey.keycode;
        for (int i = 0; i < m_priv->ncodes; ++i) {
            if (m_priv->codes[i] == code) {
                emit pressed();
                break;
            }
        }
    }
}

} // namespace zappe
