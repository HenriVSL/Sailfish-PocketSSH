#!/bin/bash
# Builds and runs the app-layer host tests in tests/app/build/.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build && cd build
qmake-qt5 ../app.pro >/dev/null
make -s -j"$(nproc)"
./tst_app "$@"
