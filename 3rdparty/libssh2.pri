# libssh2, compiled straight into the app from the vendored upstream sources
# (3rdparty/libssh2, see README.vendored there; refresh with tools/vendor-libssh2.sh).
#
# Crypto backend: the system OpenSSL 3 (libcrypto.so.3 at runtime).
# The build configuration is the hand-written 3rdparty/libssh2-config/libssh2_config.h.
#
# Everything libssh2-internal (HAVE_CONFIG_H, the config include dir, warning
# flags) goes into QMAKE_CFLAGS, so it only affects C files; the app's C++ code
# only sees the public headers in libssh2/include.

LIBSSH2_DIR = $$PWD/libssh2

INCLUDEPATH += $$LIBSSH2_DIR/include
DEPENDPATH += $$LIBSSH2_DIR/include

# Only the parts PocketSSH uses: no SFTP/SCP, ssh-agent, "publickey" subsystem or
# known_hosts file handling (the app keeps its own trusted host keys). Add
# sftp.c / scp.c / agent.c / knownhost.c / publickey.c back here when needed.
# crypto.c pulls in openssl.c, bcrypt_pbkdf.c pulls in blowfish.c.
SOURCES += \
    $$LIBSSH2_DIR/src/bcrypt_pbkdf.c \
    $$LIBSSH2_DIR/src/chacha.c \
    $$LIBSSH2_DIR/src/channel.c \
    $$LIBSSH2_DIR/src/cipher-chachapoly.c \
    $$LIBSSH2_DIR/src/comp.c \
    $$LIBSSH2_DIR/src/crypt.c \
    $$LIBSSH2_DIR/src/crypto.c \
    $$LIBSSH2_DIR/src/global.c \
    $$LIBSSH2_DIR/src/hostkey.c \
    $$LIBSSH2_DIR/src/keepalive.c \
    $$LIBSSH2_DIR/src/kex.c \
    $$LIBSSH2_DIR/src/mac.c \
    $$LIBSSH2_DIR/src/misc.c \
    $$LIBSSH2_DIR/src/packet.c \
    $$LIBSSH2_DIR/src/pem.c \
    $$LIBSSH2_DIR/src/poly1305.c \
    $$LIBSSH2_DIR/src/session.c \
    $$LIBSSH2_DIR/src/transport.c \
    $$LIBSSH2_DIR/src/userauth.c \
    $$LIBSSH2_DIR/src/userauth_kbd_packet.c \
    $$LIBSSH2_DIR/src/version.c

# libssh2's sources find their private headers next to themselves; the config
# header comes from libssh2-config. (It builds warning-free with -Wall -Wextra.)
QMAKE_CFLAGS += -DHAVE_CONFIG_H -I$$PWD/libssh2-config

# libcrypto only (not "openssl", which also pulls in libssl). Don't add
# CONFIG += link_pkgconfig here: sailfishapp.prf enables it itself, and adding it
# early makes qmake evaluate PKGCONFIG before sailfishapp adds its own package.
# Don't add QT += network either: on Sailfish OS libQt5Network links OpenSSL 1.1,
# which would put two OpenSSL versions into one process.
PKGCONFIG += libcrypto
