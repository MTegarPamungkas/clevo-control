#ifndef FANBACKEND_H
#define FANBACKEND_H

#include <QObject>
#include <QLocalSocket>
#include <QString>
#include <QTimer>

// Reads fan telemetry and requests fan changes through the daemon's control
// socket. The daemon is the sole owner of the EC.
class FanBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(int cpuTemp READ cpuTemp NOTIFY changed)
    Q_PROPERTY(int gpuTemp READ gpuTemp NOTIFY changed)
    Q_PROPERTY(int cpuRpm READ cpuRpm NOTIFY changed)
    Q_PROPERTY(int gpuRpm READ gpuRpm NOTIFY changed)
    Q_PROPERTY(int duty READ duty NOTIFY changed)
    Q_PROPERTY(bool autoMode READ autoMode NOTIFY changed)
    Q_PROPERTY(bool quietMode READ quietMode NOTIFY changed)
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
    bool quietMode() const { return m_quietMode; }
    bool connected() const { return m_connected; }
    QString fanStatus() const { return m_fanStatus; }

    // duty 0 = hand back to the firmware curve, 40..100 = fixed duty.
    Q_INVOKABLE void setDuty(int duty);
    Q_INVOKABLE void setAuto();
    // Low app-managed curve with a 40% minimum while active.
    Q_INVOKABLE void setQuiet();

signals:
    void changed();
    void fanStatusChanged();

private slots:
    void poll();

private:
    void queueCommand(const QByteArray &command);
    void finishRequest(const QString &reply);
    void updateTelemetry(const QString &reply);
    void setStatus(const QString &s);

    int m_cpuTemp = 0;
    int m_gpuTemp = -1;
    int m_cpuRpm = 0;
    int m_gpuRpm = 0;
    int m_duty = 0;
    bool m_autoMode = true;
    bool m_quietMode = false;
    bool m_connected = false;
    QString m_fanStatus;
    QLocalSocket m_socket;
    QByteArray m_request;
    QByteArray m_pendingCommand;
    QTimer m_timer;
    QTimer m_deadline;
};

#endif
