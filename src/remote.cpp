#include "remote.h"

#include "keymap.h"

#include <QFile>
#include <QThread>

#include <atomic>
#include <cstdio>

#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace zappe {
namespace {

bool nameMatches(const QString &name)
{
    const QString blob = name.toLower();
    const QString extra = qEnvironmentVariable("ZAPPE_HID_NAME").trimmed().toLower();
    if (!extra.isEmpty() && blob.contains(extra))
        return true;
    return blob.contains(QStringLiteral("xing wei")) || blob.contains(QStringLiteral("xing_wei"))
           || blob.contains(QStringLiteral("atongx")) || blob.contains(QStringLiteral("2.4g usb"));
}

#ifdef Q_OS_LINUX
struct Node {
    QString path;
    bool grab = false;
};

QVector<Node> scanNodes()
{
    QVector<Node> nodes;
    QFile file(QStringLiteral("/proc/bus/input/devices"));
    if (!file.open(QIODevice::ReadOnly))
        return nodes;
    const QString text = QString::fromUtf8(file.readAll());
    const QStringList blocks = text.split(QStringLiteral("\n\n"));
    for (const QString &block : blocks) {
        QString name;
        QStringList handlers;
        for (const QString &line : block.split(QLatin1Char('\n'))) {
            if (line.startsWith(QStringLiteral("N: Name=")))
                name = line.mid(8).trimmed().remove(QLatin1Char('"'));
            else if (line.startsWith(QStringLiteral("H: Handlers=")))
                handlers = line.mid(12).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        }
        if (!nameMatches(name))
            continue;
        QStringList events;
        bool mouse = false;
        for (const QString &handler : handlers) {
            if (handler.startsWith(QStringLiteral("event")))
                events.append(handler);
            else if (handler.startsWith(QStringLiteral("mouse")))
                mouse = true;
        }
        for (const QString &handler : events) {
            Node node;
            node.path = QStringLiteral("/dev/input/") + handler;
            // event4 na sala é o air mouse. Se o nó do mouse enumerar com outro
            // número, ainda assim captura o bloco que só tem esse event.
            node.grab = handler == QLatin1String("event4") || (mouse && events.size() == 1);
            nodes.append(node);
        }
    }
    return nodes;
}

bool bitSet(const unsigned long *bits, int code)
{
    const int width = int(sizeof(unsigned long) * 8);
    return (bits[code / width] >> (code % width)) & 1UL;
}
#endif

} // namespace

bool shouldGrabEvdevNode(bool airMouse, bool hasKeyPower, bool hasKeyUp)
{
    if (airMouse)
        return true;
    return hasKeyPower && !hasKeyUp;
}

bool hidDisabled()
{
    const QString value = qEnvironmentVariable("ZAPPE_HID_DISABLE").trimmed().toLower();
    return value == QLatin1String("1") || value == QLatin1String("true") || value == QLatin1String("yes")
           || value == QLatin1String("on");
}

class RemoteWatcher::Worker : public QThread {
public:
    explicit Worker(RemoteWatcher *owner)
        : m_owner(owner)
    {
    }
    void stop()
    {
        m_stop.store(true);
        wait(1500);
    }
    void setHoldKeyboard(bool hold) { m_holdKeyboard.store(hold); }

protected:
    void run() override
    {
#ifdef Q_OS_LINUX
        const QVector<Node> nodes = scanNodes();
        if (nodes.isEmpty()) {
            std::fprintf(stderr, "zappe: controle ATONGX não apareceu em /proc/bus/input/devices\n");
            return;
        }
        QVector<int> fds;
        QVector<pollfd> polls;
        QVector<int> keyboardFds;
        for (const Node &node : nodes) {
            const int fd = ::open(node.path.toUtf8().constData(), O_RDONLY | O_NONBLOCK);
            if (fd < 0) {
                std::fprintf(stderr, "zappe: sem leitura em %s\n", qPrintable(node.path));
                continue;
            }
            bool hasPower = false;
            bool hasUp = false;
            bool hasBack = false;
            if (!node.grab) {
                unsigned long bits[(KEY_MAX + 1) / (sizeof(unsigned long) * 8) + 1] = {};
                if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) >= 0) {
                    hasPower = bitSet(bits, KEY_POWER);
                    hasUp = bitSet(bits, KEY_UP);
                    hasBack = bitSet(bits, KEY_BACK) || bitSet(bits, KEY_BACKSPACE);
                }
            }
            const bool grab = shouldGrabEvdevNode(node.grab, hasPower, hasUp);
            if (grab && ioctl(fd, EVIOCGRAB, 1) == 0)
                std::fprintf(stderr, "zappe: grab exclusivo %s\n", qPrintable(node.path));
            else if (node.grab)
                std::fprintf(stderr, "zappe: não consegui grab em %s\n", qPrintable(node.path));
            // KEY_BACK sem KEY_UP não entra no grab inicial. Com o Chrome focado,
            // o X entrega XF86Back para a kiosk e ela vai para about:blank.
            if (!grab && (hasUp || hasBack))
                keyboardFds.append(fd);
            fds.append(fd);
            pollfd slot{};
            slot.fd = fd;
            slot.events = POLLIN;
            polls.append(slot);
        }
        bool held = false;
        int holdFails = 0;
        while (!m_stop.load()) {
            const bool wantHold = m_holdKeyboard.load();
            if (wantHold != held) {
                bool ok = true;
                for (int fd : keyboardFds) {
                    if (ioctl(fd, EVIOCGRAB, wantHold ? 1 : 0) != 0)
                        ok = false;
                }
                if (ok) {
                    held = wantHold;
                    holdFails = 0;
                    if (wantHold && !keyboardFds.isEmpty())
                        std::fprintf(stderr, "zappe: teclado do controle exclusivo\n");
                } else if (wantHold && holdFails++ % 20 == 0) {
                    std::fprintf(stderr, "zappe: não consegui segurar o teclado do controle\n");
                }
            }
            const int ready = ::poll(polls.data(), nfds_t(polls.size()), 100);
            if (ready <= 0)
                continue;
            for (int i = 0; i < polls.size(); ++i) {
                if (!(polls[i].revents & POLLIN))
                    continue;
                input_event event{};
                while (::read(fds[i], &event, sizeof(event)) == ssize_t(sizeof(event))) {
                    if (event.type != EV_KEY || event.value != 1)
                        continue;
                    const RemoteAction action = actionForEvdev(event.code);
                    if (action == RemoteAction::Unmapped || action == RemoteAction::Ignore) {
                        if (action == RemoteAction::Ignore)
                            std::fprintf(stderr, "zappe: tecla %d ignorada\n", event.code);
                        continue;
                    }
                    if (action == RemoteAction::Power)
                        std::fprintf(stderr, "zappe: power da Samsung (tecla %d); o mini-PC continua ligado\n",
                                     event.code);
                    QMetaObject::invokeMethod(m_owner, [owner = m_owner, action] {
                        emit owner->action(int(action));
                    }, Qt::QueuedConnection);
                }
            }
        }
        for (int fd : fds) {
            ioctl(fd, EVIOCGRAB, 0);
            ::close(fd);
        }
#else
        Q_UNUSED(m_owner);
#endif
    }

private:
    RemoteWatcher *m_owner = nullptr;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_holdKeyboard{false};
};

RemoteWatcher::RemoteWatcher(QObject *parent)
    : QObject(parent)
{
}

RemoteWatcher::~RemoteWatcher()
{
    if (m_worker) {
        m_worker->stop();
        delete m_worker;
    }
}

void RemoteWatcher::start()
{
    if (hidDisabled() || m_worker)
        return;
    m_worker = new Worker(this);
    m_worker->setHoldKeyboard(m_holdKeyboard);
    m_worker->start();
}

void RemoteWatcher::setHoldKeyboard(bool hold)
{
    m_holdKeyboard = hold;
    if (m_worker)
        m_worker->setHoldKeyboard(hold);
}

} // namespace zappe
