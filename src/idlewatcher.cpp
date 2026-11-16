#include "idlewatcher.h"

#include <QDir>
#include <QFile>
#include <QSocketNotifier>
#include <QDebug>

#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>

IdleWatcher::IdleWatcher(QObject *parent) : QObject(parent) {
    // Poll once a second: cheap, and the idle deadline only needs second
    // resolution.
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, &IdleWatcher::checkIdle);
    openDevice();
}

void IdleWatcher::openDevice() {
    // Find the built-in keyboard. Matching on the name is more reliable than
    // guessing event numbers, which change between boots.
    const auto nodes = QDir(QStringLiteral("/dev/input"))
                           .entryList({QStringLiteral("event*")}, QDir::System);
    for (const QString &node : nodes) {
        const QString path = QStringLiteral("/dev/input/") + node;
        QFile nameFile(QStringLiteral("/sys/class/input/") + node
                       + QStringLiteral("/device/name"));
        if (!nameFile.open(QIODevice::ReadOnly))
            continue;
        const QString name = QString::fromLocal8Bit(nameFile.readAll()).trimmed();
        if (!name.contains(QLatin1String("AT Translated"), Qt::CaseInsensitive))
            continue;

        int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            // Present but unreadable: the user is not in the `input` group yet.
            m_devicePath = path;
            emit devicePathChanged();
            return;
        }
        m_devicePath = path;
        m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this, &IdleWatcher::onReadable);
        emit devicePathChanged();
        return;
    }
    emit devicePathChanged();
}

void IdleWatcher::closeDevice() {
    if (m_notifier) {
        const int fd = m_notifier->socket();
        m_notifier->deleteLater();
        m_notifier = nullptr;
        if (fd >= 0)
            ::close(fd);
    }
}

void IdleWatcher::onReadable() {
    // Drain the pending events. Only key presses count as activity; key
    // releases, repeats and pointer-less EV_SYN noise should not keep the
    // light on forever.
    struct input_event ev;
    bool activity = false;
    while (true) {
        const ssize_t n = ::read(m_notifier->socket(), &ev, sizeof(ev));
        if (n != (ssize_t) sizeof(ev))
            break;
        if (ev.type == EV_KEY && ev.value == 1)
            activity = true;
    }
    if (!activity)
        return;

    m_idle.restart();
    if (m_dimmed) {
        m_dimmed = false;
        emit dimmedChanged();
        emit restoreRequested();
    }
}

void IdleWatcher::checkIdle() {
    if (!m_enabled || !m_notifier || m_dimmed)
        return;
    if (!m_idle.isValid())
        m_idle.start();
    if (m_idle.elapsed() < (qint64) m_timeoutSeconds * 1000)
        return;

    m_dimmed = true;
    emit dimmedChanged();
    emit dimRequested();
}

void IdleWatcher::setEnabled(bool on) {
    if (m_enabled == on)
        return;
    m_enabled = on;
    if (on) {
        m_idle.start();
        m_tick.start();
    } else {
        m_tick.stop();
        if (m_dimmed) {
            m_dimmed = false;
            emit dimmedChanged();
            emit restoreRequested();
        }
    }
    emit enabledChanged();
}

void IdleWatcher::setTimeoutSeconds(int s) {
    if (s < 5)
        s = 5;
    if (s > 3600)
        s = 3600;
    if (m_timeoutSeconds == s)
        return;
    m_timeoutSeconds = s;
    emit timeoutSecondsChanged();
}

void IdleWatcher::noteUserBrightness(int level) {
    if (level > 0)
        m_userBrightness = level;
}
