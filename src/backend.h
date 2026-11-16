#ifndef BACKEND_H
#define BACKEND_H

#include <QObject>
#include <QString>
#include <QColor>

// Talks to the Clevo keyboard backlight through the ACPI method
// \_SB_.WMI_.SCMD with command id 0x67, which the DSDT forwards to the
// embedded controller. Writing the call needs root, so the app is a thin
// front end that runs `kbdlight-ctl` (setuid) through pkexec-free sudo-less
// path: the helper is setuid root and does the write itself.
//
// Every failure is surfaced to the UI; nothing is silently ignored, because
// the whole point of this app is telling the user whether the hardware
// actually took the command.
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

    // Re-detect the helper and the ACPI method.
    Q_INVOKABLE void refresh();

    // Send the stored brightness and color again.
    Q_INVOKABLE void apply();

    Q_INVOKABLE void turnOff();

    // Send a raw ARGS dword. Used by the diagnostics panel to identify which
    // EC channel drives which colour on this particular panel.
    Q_INVOKABLE void applyRaw(const QString &hexDword);

signals:
    void brightnessChanged();
    void colorChanged();
    void availableChanged();
    void statusChanged();

private:
    bool runHelper(const QStringList &args, QString *err);
    void setStatus(const QString &s);

    int m_brightness = 9;
    QColor m_color = QColor(255, 255, 255);
    bool m_available = false;
    QString m_status;
};

#endif
