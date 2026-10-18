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
        for (const Node &node : nodes) {
            const int fd = ::open(node.path.toUtf8().constData(), O_RDONLY | O_NONBLOCK);
            if (fd < 0) {
                std::fprintf(stderr, "zappe: sem leitura em %s\n", qPrintable(node.path));
                continue;
            }
            bool grab = node.grab;
            if (!grab) {
                unsigned long bits[(KEY_MAX + 1) / (sizeof(unsigned long) * 8) + 1] = {};
                if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) >= 0 && bitSet(bits, KEY_POWER)
                    && !bitSet(bits, KEY_UP))
                    grab = true;
            }
            if (grab && ioctl(fd, EVIOCGRAB, 1) == 0)
                std::fprintf(stderr, "zappe: grab exclusivo %s\n", qPrintable(node.path));
            else if (node.grab)
                std::fprintf(stderr, "zappe: não consegui grab em %s\n", qPrintable(node.path));
            fds.append(fd);
            pollfd slot{};
            slot.fd = fd;
            slot.events = POLLIN;
            polls.append(slot);
        }
        while (!m_stop.load()) {
            const int ready = ::poll(polls.data(), nfds_t(polls.size()), 400);
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
    m_worker->start();
}

} // namespace zappe
