#ifndef FANBACKEND_H
#define FANBACKEND_H

#include <QObject>
#include <QString>
#include <QTimer>

// Reads fan telemetry and requests fan changes through the daemon's control
// socket. The GUI must never touch the EC itself: the daemon polls it every
// 200 ms and a second reader corrupts the replies, which is how impossible
// readings like "25978 RPM" appeared before.
//
// When the daemon is not running the socket is simply absent; the UI then
// says so instead of showing stale or fabricated numbers.
class FanBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(int cpuTemp READ cpuTemp NOTIFY changed)
    Q_PROPERTY(int gpuTemp READ gpuTemp NOTIFY changed)
    Q_PROPERTY(int cpuRpm READ cpuRpm NOTIFY changed)
    Q_PROPERTY(int gpuRpm READ gpuRpm NOTIFY changed)
    Q_PROPERTY(int duty READ duty NOTIFY changed)
    Q_PROPERTY(bool autoMode READ autoMode NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QString fanStatus READ fanStatus NOTIFY fanStatusChanged)

public:
    explicit FanBackend(QObject *parent = nullptr);

    int cpuTemp() const { return m_cpuTemp; }
    int gpuTemp() const { return m_gpuTemp; }
    int cpuRpm() const { return m_cpuRpm; }
    int gpuRpm() const { return m_gpuRpm; }
    int duty() const { return m_duty; }
    bool autoMode() const { return m_autoMode; }
    bool connected() const { return m_connected; }
    QString fanStatus() const { return m_fanStatus; }

    // duty 0 = hand back to the firmware curve, 40..100 = fixed duty.
    Q_INVOKABLE void setDuty(int duty);
    Q_INVOKABLE void setAuto();

signals:
    void changed();
    void fanStatusChanged();

private slots:
    void poll();

private:
    QString request(const QString &cmd, bool *ok);
    void setStatus(const QString &s);

    int m_cpuTemp = 0;
    int m_gpuTemp = -1;
    int m_cpuRpm = 0;
    int m_gpuRpm = 0;
    int m_duty = 0;
    bool m_autoMode = true;
    bool m_connected = false;
    QString m_fanStatus;
    QTimer m_timer;
};

#endif
