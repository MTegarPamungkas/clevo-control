#include "fanbackend.h"

#include <QRegularExpression>

static const char *SOCK_PATH = "/run/clevo-indicator-dual.sock";
static constexpr int POLL_MS = 700;
static constexpr int REQUEST_TIMEOUT_MS = 2500;

FanBackend::FanBackend(QObject *parent) : QObject(parent) {
    m_timer.setSingleShot(true);
    m_deadline.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &FanBackend::poll);
    connect(&m_deadline, &QTimer::timeout, this, [this] { finishRequest({}); });
    connect(&m_socket, &QLocalSocket::connected, this, [this] {
        m_socket.write(m_request + '\n');
        m_socket.flush();
    });
    connect(&m_socket, &QLocalSocket::readyRead, this, [this] {
        if (m_socket.canReadLine())
            finishRequest(QString::fromLatin1(m_socket.readLine()).trimmed());
    });
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this] {
        finishRequest({});
    });
    m_timer.start(0);
}

void FanBackend::setStatus(const QString &s) {
    if (m_fanStatus == s)
        return;
    m_fanStatus = s;
    emit fanStatusChanged();
}

void FanBackend::queueCommand(const QByteArray &command) {
    // Only the latest requested mode matters. Send it after any request
    // already in flight, ahead of the next status poll.
    m_pendingCommand = command;
    if (m_request.isEmpty())
        m_timer.start(0);
}

void FanBackend::poll() {
    m_request = m_pendingCommand.isEmpty() ? QByteArray("status") : m_pendingCommand;
    m_pendingCommand.clear();
    m_deadline.start(REQUEST_TIMEOUT_MS);
    m_socket.connectToServer(QString::fromLatin1(SOCK_PATH));
}

void FanBackend::finishRequest(const QString &reply) {
    if (m_request.isEmpty())
        return;

    const bool isStatus = m_request == "status";
    m_request.clear();
    m_deadline.stop();
    m_socket.abort();

    if (isStatus) {
        updateTelemetry(reply);
    } else if (reply != QLatin1String("ok")) {
        setStatus(reply.isEmpty() ? tr("Fan command timed out or disconnected")
                                  : tr("Fan request failed: %1").arg(reply));
    }

    const bool refresh = !isStatus && reply == QLatin1String("ok");
    m_timer.start(refresh || !m_pendingCommand.isEmpty() ? 0 : POLL_MS);
}

void FanBackend::updateTelemetry(const QString &reply) {
    static const QRegularExpression re(QStringLiteral(
            "^ok\\s+cpu=(-?\\d+)\\s+gpu=(-?\\d+)\\s+rpm1=(\\d+)\\s+rpm2=(\\d+)\\s+duty=(\\d+)\\s+auto=(\\d+)(?:\\s+mode=(\\w+))?$"));
    const auto m = re.match(reply);
    if (!m.hasMatch()) {
        if (m_connected) {
            m_connected = false;
            emit changed();
        }
        setStatus(reply.isEmpty() ? tr("Fan daemon unavailable or slow to respond")
                                  : tr("Unexpected reply: %1").arg(reply));
        return;
    }

    const int cpu = m.captured(1).toInt();
    const int gpu = m.captured(2).toInt();
    const int r1 = m.captured(3).toInt();
    const int r2 = m.captured(4).toInt();
    const int duty = m.captured(5).toInt();
    const bool autoFlag = m.captured(6).toInt() != 0;
    QString mode = m.captured(7);
    if (mode.isEmpty())
        mode = autoFlag ? QStringLiteral("auto") : QStringLiteral("manual");
    const bool manualMode = mode == QLatin1String("manual");
    const bool quietMode = mode == QLatin1String("quiet");
    const bool autoMode = !manualMode;

    if (!m_connected || cpu != m_cpuTemp || gpu != m_gpuTemp || r1 != m_cpuRpm || r2 != m_gpuRpm
            || duty != m_duty || autoMode != m_autoMode || quietMode != m_quietMode) {
        m_connected = true;
        m_cpuTemp = cpu;
        m_gpuTemp = gpu;
        m_cpuRpm = r1;
        m_gpuRpm = r2;
        m_duty = duty;
        m_autoMode = autoMode;
        m_quietMode = quietMode;
        emit changed();
    }
    if (quietMode)
        setStatus(tr("Quiet curve (app)"));
    else if (manualMode)
        setStatus(tr("Manual %1%").arg(duty));
    else
        setStatus(tr("Auto fan curve"));
}

void FanBackend::setDuty(int duty) {
    queueCommand("set " + QByteArray::number(duty));
}

void FanBackend::setAuto() {
    setDuty(0);
}

void FanBackend::setQuiet() {
    queueCommand("set quiet");
}
