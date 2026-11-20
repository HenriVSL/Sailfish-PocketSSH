# Feed-throughput benchmark without QtTest, so it can also be cross-built for the
# phone (the Sailfish build targets have no QtTest):
#   tests/terminal/bench-device.sh   (builds for aarch64 and runs it on the phone)
TEMPLATE = app
TARGET = terminal_bench
QT -= gui
CONFIG += console
include(../common.pri)
SOURCES += main.cpp
