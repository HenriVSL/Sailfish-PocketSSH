TEMPLATE = app
TARGET = terminal_demo
QT += quick
CONFIG += terminalitem
include(../common.pri)
SOURCES += main.cpp
LIBS += -lutil
