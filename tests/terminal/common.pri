# Shared by the host test programs: the terminal sources and vendored libvterm,
# built against the host Qt (5.15) with the same flags as the app where it matters.
POCKETSSH = $$PWD/../..
CONFIG += c++14
CONFIG += object_parallel_to_source
INCLUDEPATH += $$POCKETSSH/src
DEFINES += QT_DEPRECATED_WARNINGS
include($$POCKETSSH/3rdparty/libvterm.pri)
SOURCES += $$POCKETSSH/src/terminal/terminal.cpp
HEADERS += $$POCKETSSH/src/terminal/terminal.h $$POCKETSSH/src/terminal/terminal_p.h

terminalitem {
    QT += quick
    SOURCES += $$files($$POCKETSSH/src/terminal/terminalitem*.cpp) $$POCKETSSH/src/terminal/boxdrawing.cpp
    HEADERS += $$POCKETSSH/src/terminal/terminalitem.h $$POCKETSSH/src/terminal/boxdrawing.h
}
