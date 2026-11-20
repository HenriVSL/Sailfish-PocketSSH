#!/bin/bash
# Host tests for the SSH layer (src/ssh + vendored libssh2).
#
# usage: tests/ssh/run.sh [--openssl 3|system|both] [--emu] [-- test function names / QtTest args]
#
# Builds tests/ssh/tst_sshsession.pro with the host Qt 5 (qmake-qt5), starts three
# throwaway sshd instances on 127.0.0.1 as the current user (one per host key
# type, own keys and authorized_keys in tests/ssh/build/keys), runs the tests and
# stops the servers again.
#   --openssl 3       build against the host's OpenSSL 3 (/usr/lib/openssl-3, same
#                     major version as Sailfish OS); default when available
#   --openssl system  build against the default host OpenSSL (e.g. 4.x)
#   --openssl both    run the suite once with each
#   SSHTEST_BIN=path  run that prebuilt test binary instead of building one
#   --emu             also run the remoteSailfish test against an emulator's sshd,
#                     through `sfdev run emu` (takes the device lock)
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
B="$HERE/build"
OSSL3_PC=/usr/lib/openssl-3/pkgconfig

openssl_mode=
emu=0
args=()
while [ $# -gt 0 ]; do
    case "$1" in
        --openssl) openssl_mode=$2; shift 2 ;;
        --emu) emu=1; shift ;;
        --) shift; args+=("$@"); break ;;
        *) args+=("$1"); shift ;;
    esac
done
if [ -z "$openssl_mode" ]; then
    if [ -f "$OSSL3_PC/libcrypto.pc" ]; then openssl_mode=3; else openssl_mode=system; fi
fi

SSHD=$(command -v sshd || echo /usr/sbin/sshd)
[ -x "$SSHD" ] || { echo "sshd not found" >&2; exit 1; }

# ---- keys -----------------------------------------------------------------
K="$B/keys"
rm -rf "$K"
mkdir -p "$K"
chmod 700 "$K"
gen() { # file type [extra ssh-keygen args...]
    local f=$1 t=$2; shift 2
    ssh-keygen -q -t "$t" -C "pocketssh-test-$f" -f "$K/$f" "$@" </dev/null
}
gen host_ed25519 ed25519 -N ''
gen host_ecdsa ecdsa -b 256 -N ''
gen host_rsa rsa -b 3072 -N ''
gen id_ed25519 ed25519 -N ''
gen id_rsa rsa -b 3072 -N ''
gen id_rsa_pem rsa -b 2048 -m PEM -N ''
gen id_rsa_pkcs8 rsa -b 2048 -m PKCS8 -N ''
gen id_ecdsa ecdsa -b 256 -N ''
gen id_ecdsa384_pem ecdsa -b 384 -m PEM -N ''
gen id_ecdsa521 ecdsa -b 521 -N ''
gen id_ed25519_enc ed25519 -N 'correct horse'
gen id_rsa_pem_enc rsa -b 2048 -m PEM -N 'correct horse'
gen id_ecdsa_pkcs8_enc ecdsa -b 256 -m PKCS8 -N 'correct horse'
gen id_wrong_rsa rsa -b 2048 -N ''
gen id_wrong_ed25519 ed25519 -N ''
for f in "$K"/id_*.pub; do
    case "$f" in *id_wrong_*) continue ;; esac
    cat "$f"
done > "$K/authorized_keys"
chmod 600 "$K/authorized_keys"
fp() { ssh-keygen -l -E sha256 -f "$K/$1.pub" | awk '{print $2}'; }

# ---- servers ----------------------------------------------------------------
free_port() {
    python3 -I -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()'
}
pids=()
cleanup() {
    for p in "${pids[@]}"; do kill "$p" 2>/dev/null || true; done
    wait 2>/dev/null || true
}
trap cleanup EXIT

start_sshd() { # name hostkey [password yes|no] [kbd-interactive yes|no] -> prints port
    local name=$1 hostkey=$2 password=${3:-no} kbd=${4:-${3:-no}} port
    port=$(free_port)
    cat > "$K/sshd_$name.conf" <<EOF
Port $port
ListenAddress 127.0.0.1
HostKey $K/$hostkey
PidFile $K/sshd_$name.pid
AuthorizedKeysFile $K/authorized_keys
UsePAM no
StrictModes no
PubkeyAuthentication yes
# A non-root sshd can't check passwords (no access to /etc/shadow), so with
# "yes" every password is refused: enough to test the failure + retry path.
PasswordAuthentication $password
KbdInteractiveAuthentication $kbd
PerSourcePenalties no
MaxAuthTries 6
MaxStartups 50
PrintMotd no
PrintLastLog no
ForceCommand /bin/sh
LogLevel VERBOSE
EOF
    "$SSHD" -D -e -f "$K/sshd_$name.conf" -E "$K/sshd_$name.log" &
    pids+=($!)
    for _ in $(seq 100); do
        if (exec 3<>"/dev/tcp/127.0.0.1/$port") 2>/dev/null; then
            echo "$port"; return 0
        fi
        sleep 0.05
    done
    echo "sshd $name did not start; see $K/sshd_$name.log" >&2
    cat "$K/sshd_$name.log" >&2 || true
    return 1
}
# Not $(start_sshd ...): a subshell would lose the pid; redirect to a file instead.
start_sshd ed25519 host_ed25519 > "$K/port_ed25519"
start_sshd ecdsa host_ecdsa no yes > "$K/port_ecdsa"
start_sshd rsa host_rsa yes > "$K/port_rsa"

export SSHTEST_DIR="$K"
export SSHTEST_USER=$(id -un)
export SSHTEST_PORT_ED25519=$(cat "$K/port_ed25519")
export SSHTEST_PORT_ECDSA=$(cat "$K/port_ecdsa")
export SSHTEST_PORT_RSA=$(cat "$K/port_rsa")
export SSHTEST_FP_ED25519=$(fp host_ed25519)
export SSHTEST_FP_ECDSA=$(fp host_ecdsa)
export SSHTEST_FP_RSA=$(fp host_rsa)
export SSHTEST_CLOSED_PORT=$(free_port)

# ---- build + run ------------------------------------------------------------
build() { # mode -> prints binary path
    local mode=$1 dir="$B/openssl-$1"
    mkdir -p "$dir"
    (
        cd "$dir"
        if [ "$mode" = 3 ]; then export PKG_CONFIG_PATH="$OSSL3_PC"; fi
        qmake-qt5 "$HERE/tst_sshsession.pro" >/dev/null
        make -s -j"$(nproc)" >/dev/null
    ) >&2 || { echo "build failed" >&2; return 1; }
    echo "$dir/tst_sshsession"
}

modes=("$openssl_mode")
[ "$openssl_mode" = both ] && modes=(3 system)
status=0
for m in "${modes[@]}"; do
    # SSHTEST_BIN: run a prebuilt test binary (e.g. a sanitizer build) instead.
    if [ -n "${SSHTEST_BIN:-}" ]; then bin=$SSHTEST_BIN; m=prebuilt; else bin=$(build "$m") || exit 1; fi
    crypto=$(ldd "$bin" | awk '/libcrypto/ {print $3}')
    echo "=== OpenSSL mode '$m' (libcrypto: $crypto)"
    "$bin" "${args[@]}" || status=1
done

if [ "$emu" = 1 ]; then
    echo "=== remoteSailfish on an emulator (sfdev run emu)"
    (cd "$ROOT" && sfdev run emu --wait 90s -- \
        env SSHTEST_REMOTE_KEY="$HOME/SailfishOS/vmshare/ssh/private_keys/sdk" \
        "$bin" remoteSailfish) || status=1
fi
exit $status
