#!/bin/bash
# Vendor the unmodified upstream libssh2 sources into 3rdparty/libssh2/.
#
# usage: tools/vendor-libssh2.sh
#
# To upgrade: change VERSION and SHA256 below (take the checksum from a tarball
# whose GPG signature you have verified), run this script, rebuild, run
# tests/ssh/run.sh. The build glue (3rdparty/libssh2.pri and
# 3rdparty/libssh2-config/libssh2_config.h) is ours and is not touched here.
#
# The download is treated as untrusted data: it goes into a fresh temporary
# directory, its SHA-256 must match the pinned value, and its upstream GPG
# signature (Daniel Stenberg's release key) is checked when gpg is available.
# Nothing from the archive is executed.
set -euo pipefail

VERSION=1.11.1
SHA256=9954cb54c4f548198a7cbebad248bdc87dd64bd26185708a294b2b50771e3769  # libssh2-1.11.1.tar.xz
# Daniel Stenberg <daniel@haxx.se>, signs libssh2 (and curl) releases.
SIGNING_KEY=27EDEAF22F3ABCEB50DB9A125CC908FDB71E12C2

TARBALL="libssh2-${VERSION}.tar.xz"
URLS=("https://libssh2.org/download/${TARBALL}"
      "https://github.com/libssh2/libssh2/releases/download/libssh2-${VERSION}/${TARBALL}")
KEY_URL="https://keys.openpgp.org/vks/v1/by-fingerprint/${SIGNING_KEY}"

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEST="$ROOT/3rdparty/libssh2"

work=$(mktemp -d "${TMPDIR:-/tmp}/vendor-libssh2.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir "$work/dl" "$work/extract" "$work/out"

fetch() { # url outfile
    curl -fsSL --proto '=https' --tlsv1.2 --retry 2 -o "$2" "$1"
}

echo "Downloading $TARBALL"
ok=
for url in "${URLS[@]}"; do
    if fetch "$url" "$work/dl/$TARBALL" && fetch "$url.asc" "$work/dl/$TARBALL.asc"; then
        ok=1; break
    fi
    echo "  failed: $url" >&2
done
[ -n "$ok" ] || { echo "download failed" >&2; exit 1; }

echo "Checking SHA-256"
echo "$SHA256  $work/dl/$TARBALL" | sha256sum -c --quiet -

if [ "${LIBSSH2_SKIP_GPG:-0}" = 1 ]; then
    echo "Skipping GPG check (LIBSSH2_SKIP_GPG=1); the pinned SHA-256 matched."
elif ! command -v gpg >/dev/null; then
    echo "gpg not installed: skipping signature check; the pinned SHA-256 matched." >&2
else
    echo "Checking GPG signature (key $SIGNING_KEY)"
    export GNUPGHOME="$work/gnupg"
    mkdir -m 700 "$GNUPGHOME"
    fetch "$KEY_URL" "$work/key.asc"
    gpg --batch --quiet --import "$work/key.asc" 2>/dev/null
    status=$(gpg --batch --status-fd 1 --verify "$work/dl/$TARBALL.asc" "$work/dl/$TARBALL" 2>/dev/null || true)
    # VALIDSIG <signing-key-fpr> ... <primary-key-fpr>: accept only the pinned key.
    if ! grep -Eq "^\[GNUPG:\] VALIDSIG .* ${SIGNING_KEY}\$" <<<"$status"; then
        echo "GPG signature check FAILED" >&2
        exit 1
    fi
    echo "  good signature from $SIGNING_KEY"
fi

echo "Extracting"
# Refuse archives with absolute paths or '..' components before extracting.
if tar -tJf "$work/dl/$TARBALL" | grep -Eq '(^/|(^|/)\.\.(/|$))'; then
    echo "archive contains unsafe paths" >&2
    exit 1
fi
tar -xJf "$work/dl/$TARBALL" -C "$work/extract" --no-same-owner --no-same-permissions
src="$work/extract/libssh2-${VERSION}"
[ -d "$src/src" ] && [ -d "$src/include" ] || { echo "unexpected archive layout" >&2; exit 1; }

# Keep only what the app build needs: the library sources (OpenSSL backend; the
# other crypto backends are never compiled), the public headers and the license.
out="$work/out"
mkdir "$out/src" "$out/include"
cp "$src"/include/*.h "$out/include/"
for f in "$src"/src/*.c "$src"/src/*.h; do
    case "$(basename "$f")" in
        libgcrypt.*|mbedtls.*|os400qc3.*|wincng.*) continue ;;
    esac
    cp "$f" "$out/src/"
done
cp "$src/COPYING" "$src/RELEASE-NOTES" "$out/"
cat > "$out/README.vendored" <<EOF
libssh2 ${VERSION}, unmodified upstream sources.

Source:  https://libssh2.org/download/${TARBALL}
SHA-256: ${SHA256}
Signed:  ${SIGNING_KEY} (Daniel Stenberg)

Only src/ (minus the libgcrypt, mbedTLS, OS/400 and WinCNG backends),
include/, COPYING and RELEASE-NOTES are kept. Regenerate with
tools/vendor-libssh2.sh; the build glue is 3rdparty/libssh2.pri and
3rdparty/libssh2-config/libssh2_config.h.
EOF
chmod -R u=rwX,go=rX "$out"

rm -rf "$DEST"
mkdir -p "$(dirname "$DEST")"
cp -R "$out" "$DEST"
echo "libssh2 $VERSION vendored into ${DEST#"$ROOT"/}"
