#!/bin/sh
# Host tests for the terminal emulator (src/terminal) and vendored libvterm.
# Builds against the host Qt 5 with qmake-qt5 into tests/terminal/build/ and runs:
#   1. unit tests (tst_terminal): parsing, colors, scrollback, keys, throughput
#   2. render test: offscreen TerminalItem screenshots + touch/keyboard checks
#   3. feed benchmark (terminal_bench; bench-device.sh runs it on the phone)
# and builds the interactive demo (terminal_demo, a local shell in a window).
#
# Usage: tests/terminal/run.sh [OUTDIR]
#   OUTDIR      where the render test writes its PNGs (default tests/terminal/build/shots)
#   FONT_DIR    env: directory with DejaVuSansMono*.ttf to load if the host lacks
#               "DejaVu Sans Mono" (the font used on the phone)
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
BUILD="$HERE/build"
OUT=${1:-$BUILD/shots}
QMAKE=${QMAKE:-qmake-qt5}
JOBS=$(nproc 2>/dev/null || echo 4)

build() {
    mkdir -p "$BUILD/$1"
    (cd "$BUILD/$1" && "$QMAKE" "$HERE/$1/$1.pro" >/dev/null && make -s -j"$JOBS" >/dev/null)
}

echo "== building"
build unit
build render
build demo
build bench

status=0
echo "== unit tests"
"$BUILD/unit/tst_terminal" || status=1

echo "== render test (offscreen) -> $OUT"
mkdir -p "$OUT"
QT_QPA_PLATFORM=offscreen "$BUILD/render/terminal_render" "$OUT" ${FONT_DIR:-} || status=1

echo "== feed benchmark"
"$BUILD/bench/terminal_bench" || status=1

echo "== demo built: $BUILD/demo/terminal_demo [--font-dir DIR]"
[ $status -eq 0 ] && echo "ALL TERMINAL TESTS PASSED" || echo "TERMINAL TESTS FAILED"
exit $status
