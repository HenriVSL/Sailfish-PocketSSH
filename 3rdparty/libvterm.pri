# libvterm (MIT), the terminal emulation engine Neovim uses. Unmodified upstream
# release sources in 3rdparty/libvterm/ (see libvterm/VERSION and
# tools/vendor-libvterm.sh), compiled straight into the app.

LIBVTERM_DIR = $$PWD/libvterm

INCLUDEPATH += $$LIBVTERM_DIR/include
HEADERS += $$files($$LIBVTERM_DIR/include/*.h)
SOURCES += $$files($$LIBVTERM_DIR/src/*.c)

# Our C shim that adds SGR 2 (faint) through libvterm's parser callback table;
# the app's own .pro only globs C++ sources.
SOURCES += $$PWD/../src/terminal/vtermhooks.c

# libvterm is C99 (the compiler default gnu11 covers it). QMAKE_CFLAGS only reaches the vendored C code (the app is C++).
# Upstream builds with -Wall only; silence the extra warnings qmake's warn_on adds.
QMAKE_CFLAGS += -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
    -Wno-implicit-fallthrough -Wno-empty-body -Wno-old-style-declaration -Wno-maybe-uninitialized
