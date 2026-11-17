#include "backend.h"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

static const char *HELPER = "kbdlight-ctl";

Backend::Backend(QObject *parent) : QObject(parent) {
    // Long enough to swallow a drag, short enough to feel immediate on release.
    m_colorDebounce.setSingleShot(true);
    m_colorDebounce.setInterval(90);
    connect(&m_colorDebounce, &QTimer::timeout, this, &Backend::flushPending);
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
    setStatus(tr("Ready"));
}

void Backend::runHelper(const QStringList &args) {
    QString path = QStandardPaths::findExecutable(HELPER);
    if (path.isEmpty())
        path = QStringLiteral("/usr/local/bin/") + HELPER;

    auto *p = new QProcess(this);
    // Report failures from the exit handler, never by blocking the caller.
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, p](int code, QProcess::ExitStatus st) {
                if (st != QProcess::NormalExit || code != 0) {
                    const QString err = QString::fromLocal8Bit(p->readAllStandardError()).trimmed();
                    setStatus(err.isEmpty() ? tr("Helper failed") : err);
                }
                p->deleteLater();
            });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError) {
        setStatus(tr("Helper error: %1").arg(p->errorString()));
        p->deleteLater();
    });
    p->start(path, args);
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

    runHelper({QStringLiteral("brightness"), QString::number(level)});
    setStatus(level == 0 ? tr("Backlight off") : tr("Brightness %1").arg(level));
}

void Backend::setColor(const QColor &c) {
    if (!c.isValid() || m_color == c)
        return;
    m_color = c;
    emit colorChanged();
    scheduleColor();
}

void Backend::scheduleColor() {
    // Restarting the timer on every update means only the last colour of a
    // drag reaches the hardware, and the UI never waits for it.
    m_pendingColor = true;
    m_colorDebounce.start();
}

void Backend::flushPending() {
    if (!m_pendingColor)
        return;
    m_pendingColor = false;
    const QColor c = m_color;
    runHelper({QStringLiteral("color"), QString::number(c.red()),
               QString::number(c.green()), QString::number(c.blue())});
    setStatus(tr("Colour %1").arg(c.name().toUpper()));
}

void Backend::apply() {
    runHelper({QStringLiteral("brightness"), QString::number(m_brightness)});
    m_pendingColor = false;
    m_colorDebounce.stop();
    const QColor c = m_color;
    runHelper({QStringLiteral("color"), QString::number(c.red()),
               QString::number(c.green()), QString::number(c.blue())});
    setStatus(tr("Applied"));
}

void Backend::forceBrightness(int level) {
    if (level < 0)
        level = 0;
    if (level > 9)
        level = 9;
    m_brightness = level;
    emit brightnessChanged();
    runHelper({QStringLiteral("brightness"), QString::number(level)});
    setStatus(level == 0 ? tr("Backlight off") : tr("Brightness %1").arg(level));
}

void Backend::forceColor(const QColor &c) {
    if (!c.isValid())
        return;
    m_color = c;
    m_colorDebounce.stop();
    m_pendingColor = false;
    emit colorChanged();
    runHelper({QStringLiteral("color"), QString::number(c.red()),
               QString::number(c.green()), QString::number(c.blue())});
    setStatus(tr("Colour %1").arg(c.name().toUpper()));
}

void Backend::forceBrightnessBlocking(int level) {
    if (level < 0)
        level = 0;
    if (level > 9)
        level = 9;
    m_brightness = level;

    QString path = QStandardPaths::findExecutable(HELPER);
    if (path.isEmpty())
        path = QStringLiteral("/usr/local/bin/") + HELPER;

    QProcess p;
    p.start(path, {QStringLiteral("brightness"), QString::number(level)});
    // Wait for the helper to finish, otherwise the process is killed when the
    // app exits and the light stays off.
    p.waitForFinished(3000);
}

void Backend::turnOff() {
    runHelper({QStringLiteral("off")});
    setStatus(tr("Backlight off"));
}

void Backend::applyRaw(const QString &hexDword) {
    runHelper({QStringLiteral("raw"), hexDword});
    setStatus(tr("Sent %1").arg(hexDword));
}
