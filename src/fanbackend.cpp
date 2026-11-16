#include "fanbackend.h"

#include <QLocalSocket>
#include <QRegularExpression>

static const char *SOCK_PATH = "/run/clevo-indicator-dual.sock";

FanBackend::FanBackend(QObject *parent) : QObject(parent) {
    // The daemon samples the EC every 200 ms, so polling faster than that
    // would only add load without new information.
    m_timer.setInterval(700);
    connect(&m_timer, &QTimer::timeout, this, &FanBackend::poll);
    m_timer.start();
    poll();
}

void FanBackend::setStatus(const QString &s) {
    if (m_fanStatus == s)
        return;
    m_fanStatus = s;
    emit fanStatusChanged();
}

QString FanBackend::request(const QString &cmd, bool *ok) {
    QLocalSocket sock;
    sock.connectToServer(QString::fromLatin1(SOCK_PATH));
    if (!sock.waitForConnected(400)) {
        if (ok)
            *ok = false;
        return QString();
    }
    sock.write(cmd.toLatin1() + '\n');
    sock.flush();
    if (!sock.waitForReadyRead(800)) {
        if (ok)
            *ok = false;
        return QString();
    }
    const QString reply = QString::fromLatin1(sock.readAll()).trimmed();
    sock.disconnectFromServer();
    if (ok)
        *ok = reply.startsWith(QLatin1String("ok"));
    return reply;
}

void FanBackend::poll() {
    bool ok = false;
    const QString reply = request(QStringLiteral("status"), &ok);

    if (m_connected != ok) {
        m_connected = ok;
        emit changed();
    }
    if (!ok) {
        setStatus(tr("Fan daemon not running — start clevo-indicator-dual"));
        return;
    }

    static const QRegularExpression re(QStringLiteral(
            "cpu=(-?\\d+)\\s+gpu=(-?\\d+)\\s+rpm1=(\\d+)\\s+rpm2=(\\d+)\\s+duty=(\\d+)\\s+auto=(\\d+)"));
    const auto m = re.match(reply);
    if (!m.hasMatch()) {
        setStatus(tr("Unexpected reply: %1").arg(reply));
        return;
    }

    const int cpu = m.captured(1).toInt();
    const int gpu = m.captured(2).toInt();
    const int r1 = m.captured(3).toInt();
    const int r2 = m.captured(4).toInt();
    const int duty = m.captured(5).toInt();
    const bool autoMode = m.captured(6).toInt() != 0;

    if (cpu != m_cpuTemp || gpu != m_gpuTemp || r1 != m_cpuRpm || r2 != m_gpuRpm
            || duty != m_duty || autoMode != m_autoMode) {
        m_cpuTemp = cpu;
        m_gpuTemp = gpu;
        m_cpuRpm = r1;
        m_gpuRpm = r2;
        m_duty = duty;
        m_autoMode = autoMode;
        emit changed();
    }
    setStatus(autoMode ? tr("Firmware curve") : tr("Manual %1%").arg(duty));
}

void FanBackend::setDuty(int duty) {
    bool ok = false;
    const QString reply = request(QStringLiteral("set %1").arg(duty), &ok);
    if (!ok)
        setStatus(tr("Fan request failed: %1").arg(reply));
    else
        poll();
}

void FanBackend::setAuto() {
    setDuty(0);
}
