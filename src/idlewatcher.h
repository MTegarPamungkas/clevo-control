#ifndef IDLEWATCHER_H
#define IDLEWATCHER_H

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

class QSocketNotifier;

// Turns the keyboard backlight off after the machine has been idle for a
// while, and back on as soon as the user types again.
//
// Idle is detected by reading the raw key events from the keyboard's
// /dev/input/event* node. logind's IdleHint is not usable here: Hyprland never
// reports idle to it, so IdleHint stays false and IdleSinceHint stays 0.
// Reading the event node needs membership of the `input` group, which the
// udev rule in pkgbuild/90-kbdlight-input.rules grants.
//
// The watcher only ever restores the brightness that was in use before it
// turned the light off, so it never fights the user's own setting.
class IdleWatcher : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(int timeoutSeconds READ timeoutSeconds WRITE setTimeoutSeconds NOTIFY timeoutSecondsChanged)
    Q_PROPERTY(bool dimmed READ dimmed NOTIFY dimmedChanged)
    Q_PROPERTY(QString devicePath READ devicePath NOTIFY devicePathChanged)
    Q_PROPERTY(bool deviceAvailable READ deviceAvailable NOTIFY devicePathChanged)

public:
    explicit IdleWatcher(QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);

    int timeoutSeconds() const { return m_timeoutSeconds; }
    void setTimeoutSeconds(int s);

    bool dimmed() const { return m_dimmed; }
    QString devicePath() const { return m_devicePath; }
    bool deviceAvailable() const { return m_notifier != nullptr; }

    // Remember the brightness the user wants, so a restore puts it back.
    Q_INVOKABLE void noteUserBrightness(int level);

signals:
    void enabledChanged();
    void timeoutSecondsChanged();
    void dimmedChanged();
    void devicePathChanged();

    // Emitted when the watcher wants the backlight turned off / restored.
    void dimRequested();
    void restoreRequested();

private slots:
    void onReadable();
    void checkIdle();

private:
    void openDevice();
    void closeDevice();

    bool m_enabled = false;
    int m_timeoutSeconds = 60;
    bool m_dimmed = false;
    QString m_devicePath;
    QSocketNotifier *m_notifier = nullptr;
    QElapsedTimer m_idle;
    QTimer m_tick;
    int m_userBrightness = 9;
};

#endif
