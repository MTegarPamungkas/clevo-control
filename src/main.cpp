#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

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

    Theme theme;
    Backend backend;
    FanBackend fan;
    IdleWatcher idle;

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

    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
