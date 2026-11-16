#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "backend.h"
#include "fanbackend.h"
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

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("fan"), &fan);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
