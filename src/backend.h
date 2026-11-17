#ifndef BACKEND_H
#define BACKEND_H

#include <QColor>
#include <QObject>
#include <QString>
#include <QTimer>

// Talks to the Clevo keyboard backlight through a small setuid helper that
// writes one ACPI call. The GUI never runs as root and never blocks on it:
// dragging a colour picker fires dozens of updates per second, and waiting
// for a subprocess each time froze the UI thread mid-drag (the marker stuck
// where the freeze began). Preview is instant; the hardware write is
// debounced through a timer.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(int brightness READ brightness WRITE setBrightness NOTIFY brightnessChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit Backend(QObject *parent = nullptr);

    int brightness() const { return m_brightness; }
    void setBrightness(int level);

    QColor color() const { return m_color; }
    void setColor(const QColor &c);

    bool available() const { return m_available; }
    QString status() const { return m_status; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void apply();
    Q_INVOKABLE void turnOff();
    Q_INVOKABLE void applyRaw(const QString &hexDword);

    // Write a level to the hardware even when it matches the stored one.
    // Needed by the idle watcher: turning the light off leaves m_brightness
    // untouched, so restoring it would otherwise be skipped as a no-op.
    Q_INVOKABLE void forceBrightness(int level);

    // The startup counterpart for the saved colour: the EC may still hold a
    // different colour, so the write must happen even if it is plain white.
    void forceColor(const QColor &c);

    // Blocking variant used on shutdown. The normal path is asynchronous so
    // the UI never stalls, but at exit there is no event loop left to deliver
    // the finished signal, and the helper would be killed mid-write.
    void forceBrightnessBlocking(int level);

signals:
    void brightnessChanged();
    void colorChanged();
    void availableChanged();
    void statusChanged();

private slots:
    void flushPending();

private:
    // Runs the helper without blocking the event loop. The reply is handled
    // when it arrives, so the UI stays responsive even if the EC is slow.
    void runHelper(const QStringList &args);
    void scheduleColor();
    void setStatus(const QString &s);

    int m_brightness = 9;
    QColor m_color = QColor(255, 255, 255);
    bool m_available = false;
    QString m_status;

    // Coalesces rapid colour changes into one hardware write.
    QTimer m_colorDebounce;
    bool m_pendingColor = false;
};

#endif
