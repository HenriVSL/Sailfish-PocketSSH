#!/bin/bash
# Renders the Sailfish launcher icons from the Pocket Shell (iOS) app icon.
#
#   tools/make-icons.sh [SOURCE.png]
#
# SOURCE defaults to icons/src/pocket-shell-ios-1024.png, a copy of
# "Pocket Shell/Assets.xcassets/AppIcon.appiconset/AppIcon.png" from
# https://github.com/HenriVSL/Pocket-Shell (same author, MIT). It is a full-bleed
# 1024x1024 square; iOS rounds the corners itself, Sailfish does not.
#
# Sailfish app icons bring their own shape (Jolla "Apps icon story"): a base shape
# that fills the whole icon, built from a square and a circle, each corner either
# fully round (radius = half the icon) or almost square. We use the classic one:
# three round corners and a near-square top-right corner, transparent outside the
# shape, with the ~0.3 px edge gap (at 86 px) the guide asks for. The artwork is
# shrunk a little first so the monitor stays clear of the round corners.
#
# Writes icons/{86x86,108x108,128x128,172x172}/harbour-pocketssh.png.
# Needs ImageMagick 7 (`magick`) and rsvg-convert (librsvg).
set -euo pipefail

cd "$(dirname "$0")/.."
src=${1:-icons/src/pocket-shell-ios-1024.png}
name=harbour-pocketssh
sizes="86 108 128 172"
master=1024        # work size
scale=0.88         # artwork scale inside the shape
corner=0.035       # near-square corner radius, as a fraction of the icon size
gap=0.0035         # edge gap per side (0.3 px at 86 px)

for tool in magick rsvg-convert; do
    command -v "$tool" >/dev/null || { echo "make-icons: $tool not found" >&2; exit 1; }
done
[ -f "$src" ] || { echo "make-icons: source $src not found" >&2; exit 1; }

tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT

# The base shape as SVG, in a 1x1 box scaled to the master size.
awk -v S="$master" -v c="$corner" -v g="$gap" 'BEGIN {
    a = g * S; b = S - g * S; w = b - a
    R = w / 2; r = c * S
    printf "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\">\n", S, S, S, S
    # clockwise from the top edge: top-right small, bottom-right round,
    # bottom-left round, top-left round
    printf "<path fill=\"#fff\" d=\"M %f %f H %f A %f %f 0 0 1 %f %f V %f A %f %f 0 0 1 %f %f H %f A %f %f 0 0 1 %f %f V %f A %f %f 0 0 1 %f %f Z\"/>\n",
        a + R, a,  b - r,  r, r, b, a + r,  b - R,  R, R, b - R, b,  a + R,  R, R, a, b - R,  a + R,  R, R, a + R, a
    print "</svg>"
}' > "$tmp/shape.svg"
rsvg-convert -w "$master" -h "$master" "$tmp/shape.svg" -o "$tmp/shape.png"

# Shrink the artwork around its centre; the background is a smooth gradient, so
# repeating its edge pixels fills the border without a visible seam.
magick "$src" -resize "${master}x${master}!" -virtual-pixel edge \
    -distort SRT "$scale 0" "$tmp/art.png"

# Cut the shape out: alpha = shape coverage.
magick "$tmp/art.png" \( "$tmp/shape.png" -alpha extract \) \
    -alpha off -compose CopyOpacity -composite "$tmp/master.png"

for s in $sizes; do
    mkdir -p "icons/${s}x${s}"
    magick "$tmp/master.png" -filter Lanczos -resize "${s}x${s}" \
        -strip -define png:color-type=6 "icons/${s}x${s}/$name.png"
    echo "icons/${s}x${s}/$name.png"
done
