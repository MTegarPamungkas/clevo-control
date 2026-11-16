QT += quick network
CONFIG += c++17
CONFIG -= app_bundle

TARGET = kbdlight
TEMPLATE = app

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/fanbackend.cpp \
    src/theme.cpp

HEADERS += \
    src/backend.h \
    src/fanbackend.h \
    src/theme.h

RESOURCES += src/resources.qrc

target.path = /usr/local/bin
INSTALLS += target
