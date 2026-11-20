TEMPLATE = app
TARGET = tst_terminal
QT += testlib
QT -= gui
CONFIG += console testcase
include(../common.pri)
SOURCES += tst_terminal.cpp
