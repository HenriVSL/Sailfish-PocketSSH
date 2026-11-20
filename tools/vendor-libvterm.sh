#!/bin/sh
# Vendor the libvterm release sources into 3rdparty/libvterm/.
#
# Downloads the pinned release tarball into a fresh temporary directory, checks
# its SHA-256, and copies the unmodified library sources (src/, include/, LICENSE)
# into 3rdparty/libvterm/. The generated src/encoding/*.inc tables ship in the
# release tarball; if a future tarball lacks them they are generated here with the
# upstream perl script, so the RPM build never needs perl.
#
# The checksum was cross-checked against the Arch Linux PKGBUILD and the Homebrew
# formula (which downloads the same file from launchpad.net).
#
# Usage: tools/vendor-libvterm.sh   (from anywhere; needs curl, tar, sha256sum)
set -eu

VERSION=0.3.3
SHA256=09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0
URL="https://www.leonerd.org.uk/code/libvterm/libvterm-$VERSION.tar.gz"
MIRROR="https://launchpad.net/libvterm/trunk/v0.3/+download/libvterm-$VERSION.tar.gz"

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEST="$ROOT/3rdparty/libvterm"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

TARBALL="$WORK/libvterm-$VERSION.tar.gz"
echo "Downloading $URL"
if ! curl -fsSL -o "$TARBALL" "$URL"; then
    echo "Primary download failed, trying $MIRROR"
    curl -fsSL -o "$TARBALL" "$MIRROR"
fi

echo "$SHA256  $TARBALL" | sha256sum -c -

mkdir "$WORK/x"
tar -xzf "$TARBALL" -C "$WORK/x"
SRC="$WORK/x/libvterm-$VERSION"
[ -f "$SRC/include/vterm.h" ] || { echo "unexpected tarball layout" >&2; exit 1; }

# Generate the encoding tables if the tarball does not ship them.
for tbl in "$SRC"/src/encoding/*.tbl; do
    [ -e "$tbl" ] || continue
    inc="${tbl%.tbl}.inc"
    if [ ! -f "$inc" ]; then
        echo "Generating $(basename "$inc")"
        perl -CSD "$SRC/tbl2inc_c.pl" "$tbl" > "$inc"
    fi
done

rm -rf "$DEST"
mkdir -p "$DEST/src/encoding" "$DEST/include"
cp "$SRC"/include/*.h "$DEST/include/"
cp "$SRC"/src/*.c "$SRC"/src/*.h "$SRC"/src/*.inc "$DEST/src/"
cp "$SRC"/src/encoding/*.inc "$DEST/src/encoding/"
cp "$SRC/LICENSE" "$DEST/LICENSE"
printf 'libvterm %s\n%s\nsha256 %s\nUnmodified upstream sources (src/, include/, LICENSE). Refresh with tools/vendor-libvterm.sh.\n' \
    "$VERSION" "$URL" "$SHA256" > "$DEST/VERSION"

echo "Vendored libvterm $VERSION into $DEST"
