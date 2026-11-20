#!/bin/sh
# Cross-builds the feed-throughput benchmark (tests/terminal/bench) for aarch64 with
# the project's sfdk snapshot and runs it on the phone through sfdev.
#   tests/terminal/bench-device.sh [MEGABYTES]
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SFDK=${SFDK:-$HOME/SailfishOS/bin/sfdk}
B="$HERE/build/bench-aarch64"
mkdir -p "$B"
cd "$B"
flock "$ROOT/.git/sfbuild.lock" sh -c "
    [ -d .sfdk ] || '$SFDK' -c target=SailfishOS-5.1.0.11-aarch64 -c snapshot=PocketSSH build-init '$ROOT'
    '$SFDK' -c target=SailfishOS-5.1.0.11-aarch64 -c snapshot=PocketSSH build-shell \
        sh -c 'qmake -qt=5 ../../bench/bench.pro && make -s -j8'"
MB=${1:-8}
sfdev run phone --wait 5m -- sh -c "\$SFDEV_SCP terminal_bench \$SFDEV_SSH_TARGET:/tmp/pocketssh_terminal_bench &&
    \$SFDEV_SSH \$SFDEV_SSH_TARGET '/tmp/pocketssh_terminal_bench $MB; rm -f /tmp/pocketssh_terminal_bench'"
