#include "backend.h"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

static const char *HELPER = "kbdlight-ctl";

Backend::Backend(QObject *parent) : QObject(parent) {
    refresh();
}

void Backend::setStatus(const QString &s) {
    if (m_status == s)
        return;
    m_status = s;
    emit statusChanged();
}

void Backend::refresh() {
    QString path = QStandardPaths::findExecutable(HELPER);
    if (path.isEmpty())
        path = QStringLiteral("/usr/local/bin/") + HELPER;

    bool ok = QFileInfo(path).isExecutable();
    if (m_available != ok) {
        m_available = ok;
        emit availableChanged();
    }
    if (!ok) {
        setStatus(tr("Helper %1 not found — run bin/install").arg(HELPER));
        return;
    }

    QString err;
    if (!runHelper({QStringLiteral("probe")}, &err)) {
        setStatus(tr("ACPI method unreachable: %1").arg(err.trimmed()));
        return;
    }
    setStatus(tr("Ready"));
}

bool Backend::runHelper(const QStringList &args, QString *err) {
    QString path = QStandardPaths::findExecutable(HELPER);
    if (path.isEmpty())
        path = QStringLiteral("/usr/local/bin/") + HELPER;

    QProcess p;
    p.start(path, args);
    if (!p.waitForFinished(5000)) {
        p.kill();
        if (err)
            *err = tr("helper timed out");
        return false;
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (err)
            *err = QString::fromLocal8Bit(p.readAllStandardError());
        return false;
    }
    return true;
}

void Backend::setBrightness(int level) {
    if (level < 0)
        level = 0;
    if (level > 9)
        level = 9;
    if (m_brightness == level)
        return;
    m_brightness = level;
    emit brightnessChanged();

    QString err;
    if (!runHelper({QStringLiteral("brightness"), QString::number(level)}, &err))
        setStatus(tr("Brightness failed: %1").arg(err.trimmed()));
    else
        setStatus(level == 0 ? tr("Backlight off") : tr("Brightness %1").arg(level));
}

void Backend::setColor(const QColor &c) {
    if (!c.isValid() || m_color == c)
        return;
    m_color = c;
    emit colorChanged();

    // The helper rotates RGB into the BRG order this panel actually uses
    // (payload A=blue, B=red, C=green), so QML stays plain RGB and the
    // hardware quirk lives in one place.
    QString err;
    if (!runHelper({QStringLiteral("color"), QString::number(c.red()),
                    QString::number(c.green()), QString::number(c.blue())},
                   &err))
        setStatus(tr("Colour failed: %1").arg(err.trimmed()));
    else
        setStatus(tr("Colour %1").arg(c.name().toUpper()));
}

void Backend::apply() {
    QString err;
    if (!runHelper({QStringLiteral("brightness"), QString::number(m_brightness)}, &err)) {
        setStatus(tr("Brightness failed: %1").arg(err.trimmed()));
        return;
    }
    if (!runHelper({QStringLiteral("color"), QString::number(m_color.red()),
                    QString::number(m_color.green()), QString::number(m_color.blue())},
                   &err)) {
        setStatus(tr("Colour failed: %1").arg(err.trimmed()));
        return;
    }
    setStatus(tr("Applied"));
}

void Backend::turnOff() {
    QString err;
    if (!runHelper({QStringLiteral("off")}, &err))
        setStatus(tr("Off failed: %1").arg(err.trimmed()));
    else
        setStatus(tr("Backlight off"));
}

void Backend::applyRaw(const QString &hexDword) {
    QString err;
    if (!runHelper({QStringLiteral("raw"), hexDword}, &err))
        setStatus(tr("Raw write failed: %1").arg(err.trimmed()));
    else
        setStatus(tr("Sent %1").arg(hexDword));
}
