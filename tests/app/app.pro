# Host unit tests for the app layer (no device, no SSH): qmake-qt5 && make && ./tst_app
QT += testlib
QT -= gui
CONFIG += c++14 console testcase
TARGET = tst_app
INCLUDEPATH += ../../src
SOURCES += tst_app.cpp \
    ../../src/app/hostmodel.cpp \
    ../../src/app/secretstore.cpp \
    ../../src/app/knownhosts.cpp \
    ../../src/app/keyinfo.cpp \
    ../../src/app/storage.cpp \
    ../../src/app/configtransfer.cpp
HEADERS += ../../src/app/hostmodel.h ../../src/app/knownhosts.h ../../src/app/configtransfer.h
