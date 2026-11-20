# PocketSSH: an SSH client for Sailfish OS.
#
# The TARGET name must match: qml/harbour-pocketssh.qml, harbour-pocketssh.desktop,
# icons/*/harbour-pocketssh.png and rpm/harbour-pocketssh.spec.
#
# libssh2 and libvterm are not in the Sailfish OS repositories, so their unmodified
# upstream sources are vendored under 3rdparty/ and compiled straight into the app.
# libssh2 uses the system OpenSSL 3 (libcrypto.so.3) for its cryptography.

TARGET = harbour-pocketssh

CONFIG += sailfishapp c++14
# Vendored C sources share base names with each other (state.c, session.c, ...);
# keep each object next to its source path instead of one flat directory.
CONFIG += object_parallel_to_source

# Sources are picked up by directory, so new files need no change here.
SOURCES += $$files(src/*.cpp, true)
HEADERS += $$files(src/*.h, true)
INCLUDEPATH += src

# The spec's %qmake5 passes VERSION; plain qmake gets the fallback.
isEmpty(VERSION): VERSION = 1.0.0
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

include(3rdparty/libssh2.pri)
include(3rdparty/libvterm.pri)

DISTFILES += qml/harbour-pocketssh.qml \
    qml/cover/*.qml \
    qml/pages/*.qml \
    qml/components/*.qml \
    rpm/harbour-pocketssh.changes \
    rpm/harbour-pocketssh.spec \
    harbour-pocketssh.desktop

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172
