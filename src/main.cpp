#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSettings>
#include <QTimer>

#include "backend.h"
#include "fanbackend.h"
#include "idlewatcher.h"
#include "theme.h"

// The idle watcher used to live only inside the window process, so closing
// Clevo Control also switched off "turn the backlight off when idle". This
// mode runs the watcher on its own, as kbdlight-idle.service, and the window
// only edits the settings it reads.
static int runIdleDaemon(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kbdlight"));
    app.setOrganizationName(QStringLiteral("kbdlight"));

    Backend backend;
    IdleWatcher idle;
    QSettings settings;

    const QString path = settings.fileName();

    auto reload = [&]() {
        // sync() is what makes an external write by the window visible here.
        settings.sync();
        idle.setTimeoutSeconds(settings.value(QStringLiteral("idleTimeout"), 60).toInt());
        idle.setEnabled(settings.value(QStringLiteral("idleEnabled"), false).toBool());
        qInfo("kbdlight-idle: %s, timeout %ds, keyboard %s", idle.enabled() ? "enabled" : "disabled",
              idle.timeoutSeconds(), qPrintable(idle.devicePath().isEmpty() ? QStringLiteral("not found")
                                                                           : idle.devicePath()));
    };

    QObject::connect(&idle, &IdleWatcher::dimRequested, &backend, [&backend]() {
        qInfo("kbdlight-idle: keyboard idle, turning the backlight off");
        backend.turnOff();
    });
    QObject::connect(&idle, &IdleWatcher::restoreRequested, &backend, [&settings, &backend]() {
        // The level the user picked is saved on every change by the window.
        const int level = settings.value(QStringLiteral("brightness"), 8).toInt();
        qInfo("kbdlight-idle: activity, restoring brightness %d", level);
        backend.forceBrightness(level);
    });

    // QSettings may replace the file, which drops it from the watch, so re-arm
    // after every notification and watch the directory for the first save.
    QFileSystemWatcher watcher;
    QTimer debounce;
    debounce.setSingleShot(true);
    debounce.setInterval(120);
    auto rearm = [&]() {
        const auto watched = watcher.files() + watcher.directories();
        if (!watched.isEmpty())
            watcher.removePaths(watched);

        const QFileInfo info(path);
        if (QFileInfo::exists(info.absolutePath()))
            watcher.addPath(info.absolutePath());
        if (QFileInfo::exists(path))
            watcher.addPath(path);
    };
    QObject::connect(&watcher, &QFileSystemWatcher::fileChanged, &debounce,
                     [&debounce]() { debounce.start(); });
    QObject::connect(&watcher, &QFileSystemWatcher::directoryChanged, &debounce,
                     [&debounce]() { debounce.start(); });
    QObject::connect(&debounce, &QTimer::timeout, &app, [&]() {
        rearm();
        reload();
    });

    rearm();
    reload();
    qInfo("kbdlight-idle: watching %s", qPrintable(path));
    return app.exec();
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i)
        if (qstrcmp(argv[i], "--idle-daemon") == 0)
            return runIdleDaemon(argc, argv);

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kbdlight"));
    app.setApplicationDisplayName(QStringLiteral("Clevo Control"));
    app.setOrganizationName(QStringLiteral("kbdlight"));
    // Lets the compositor match the window to kbdlight.desktop and its icon.
    app.setDesktopFileName(QStringLiteral("kbdlight"));

    // Refuse to run twice. Two instances both drive the same backlight and
    // both react to idle, so quitting one looked like the other had turned
    // the light off, and the portal registration failed with "Connection
    // already associated with an application ID".
    QLockFile lock(QDir::tempPath() + QStringLiteral("/kbdlight.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(100)) {
        qWarning("Clevo Control is already running");
        return 0;
    }

    Theme theme;
    Backend backend;
    FanBackend fan;
    // Monitor only: the kbdlight-idle service owns the hardware, so closing
    // this window no longer disables the idle feature.
    IdleWatcher idle(false);

    // Restore what the user last chose. Without this every restart began from
    // the compiled-in defaults, so the backlight level and the idle setting
    // silently reset.
    QSettings settings;
    const int savedBrightness = settings.value(QStringLiteral("brightness"), 8).toInt();
    const bool savedIdle = settings.value(QStringLiteral("idleEnabled"), false).toBool();
    const int savedTimeout = settings.value(QStringLiteral("idleTimeout"), 60).toInt();
    // The colour is a choice too; without this every launch came back white.
    const QColor savedColor(settings.value(QStringLiteral("color"), QStringLiteral("#ffffff")).toString());

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("fan"), &fan);
    engine.rootContext()->setContextProperty(QStringLiteral("idle"), &idle);

    // Persist on every change, so a crash or a hard kill loses nothing. sync()
    // makes the write visible to kbdlight-idle straight away instead of when
    // QSettings decides to flush.
    QObject::connect(&backend, &Backend::brightnessChanged, [&backend, &settings]() {
        if (backend.brightness() > 0)
            settings.setValue(QStringLiteral("brightness"), backend.brightness());
        settings.sync();
    });
    QObject::connect(&backend, &Backend::colorChanged, [&backend, &settings]() {
        if (backend.color().isValid())
            settings.setValue(QStringLiteral("color"), backend.color().name(QColor::HexRgb));
        settings.sync();
    });
    QObject::connect(&idle, &IdleWatcher::enabledChanged, [&idle, &settings]() {
        settings.setValue(QStringLiteral("idleEnabled"), idle.enabled());
        settings.sync();
    });
    QObject::connect(&idle, &IdleWatcher::timeoutSecondsChanged, [&idle, &settings]() {
        settings.setValue(QStringLiteral("idleTimeout"), idle.timeoutSeconds());
        settings.sync();
    });

    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    // Apply the restored state before the window is shown.
    backend.forceBrightness(savedBrightness);
    backend.forceColor(savedColor);
    idle.setTimeoutSeconds(savedTimeout);
    idle.setEnabled(savedIdle);

    return app.exec();
}
