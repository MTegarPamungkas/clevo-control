#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QLockFile>
#include <QDir>
#include <QQmlContext>
#include <QSettings>

#include "backend.h"
#include "fanbackend.h"
#include "idlewatcher.h"
#include "theme.h"

int main(int argc, char *argv[]) {
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
    IdleWatcher idle;

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
    // The watcher asks for the light to go off when the machine goes idle and
    // to come back when the user types. Remembering the level here means a
    // restore always returns to what the user actually chose.
    QObject::connect(&idle, &IdleWatcher::dimRequested, &backend, [&backend, &idle]() {
        idle.noteUserBrightness(backend.brightness());
        backend.turnOff();
    });
    QObject::connect(&idle, &IdleWatcher::restoreRequested, &backend, [&backend, &idle]() {
        // Backend keeps the last user level; turning the light back on with it
        // is enough because turnOff() does not change the stored value.
        backend.forceBrightness(backend.brightness());
    });

    // Persist on every change, so a crash or a hard kill loses nothing.
    QObject::connect(&backend, &Backend::brightnessChanged, [&backend, &settings]() {
        if (backend.brightness() > 0)
            settings.setValue(QStringLiteral("brightness"), backend.brightness());
    });
    QObject::connect(&backend, &Backend::colorChanged, [&backend, &settings]() {
        if (backend.color().isValid())
            settings.setValue(QStringLiteral("color"), backend.color().name(QColor::HexRgb));
    });
    QObject::connect(&idle, &IdleWatcher::enabledChanged, [&idle, &settings]() {
        settings.setValue(QStringLiteral("idleEnabled"), idle.enabled());
    });
    QObject::connect(&idle, &IdleWatcher::timeoutSecondsChanged, [&idle, &settings]() {
        settings.setValue(QStringLiteral("idleTimeout"), idle.timeoutSeconds());
    });

    // Leaving the light off when the window closes was the bug: closing while
    // idle-dimmed left the backlight dark until the next launch, and nothing
    // ever turned it back on. Always hand the light back at the user's level.
    QObject::connect(&app, &QGuiApplication::aboutToQuit, [&backend, &idle]() {
        if (idle.dimmed())
            backend.forceBrightnessBlocking(backend.brightness() > 0 ? backend.brightness() : 8);
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
