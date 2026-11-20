# Host test for src/ssh (SshSession + vendored libssh2). Run with tests/ssh/run.sh.
TEMPLATE = app
TARGET = tst_sshsession
QT = core testlib
CONFIG += console c++14 testcase link_pkgconfig
CONFIG -= app_bundle

PROJECT_ROOT = $$PWD/../..
INCLUDEPATH += $$PROJECT_ROOT/src
SOURCES += tst_sshsession.cpp $$PROJECT_ROOT/src/ssh/sshsession.cpp
HEADERS += $$PROJECT_ROOT/src/ssh/sshsession.h

include($$PROJECT_ROOT/3rdparty/libssh2.pri)

# Mirror the target's Qt 5.6 constraints a little: no deprecated-API surprises.
DEFINES += QT_DEPRECATED_WARNINGS
QMAKE_CXXFLAGS += -Wall -Wextra
QMAKE_CFLAGS += -Wall -Wextra
