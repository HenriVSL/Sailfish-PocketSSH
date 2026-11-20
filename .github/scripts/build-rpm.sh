#!/bin/bash
# Builds the PocketSSH RPM for one architecture inside the Sailfish OS Platform
# SDK docker image, the same way `sfdev build` / `sfdk build` does locally:
# a shadow build of the source tree with mb2 (rpm spec + %qmake5).
#
#   .github/scripts/build-rpm.sh ARCH [OUTDIR]
#
# ARCH is aarch64, armv7hl or i486. The RPM ends up in OUTDIR (default RPMS/).
# Used by .github/workflows/build.yml; works the same on any Linux host with
# docker (no secrets, no Sailfish SDK install needed).
#
# Images: CODeRUS's Platform SDK images, built from Jolla's official SDK
# tooling and target tarballs (https://github.com/CODeRUS/docker-sailfishos-platform-sdk).
# One image per architecture, pinned by digest. To move to another SDK
# release, update SFOS_RELEASE and all three digests together, e.g. from
#   curl -s https://hub.docker.com/v2/repositories/coderus/sailfishos-platform-sdk-aarch64/tags/<release>
set -euo pipefail

SFOS_RELEASE=5.1.0.11

arch=${1:?usage: build-rpm.sh aarch64|armv7hl|i486 [OUTDIR]}
outdir=${2:-RPMS}

case "$arch" in
    aarch64) digest=sha256:5ef805767de26bbf8fb98ee61ecc94dec30e033bc90f2635f9b3ebe171a826a1 ;;
    armv7hl) digest=sha256:55b8cac75b87c76121cbfc2aa2cf0f2f8870a1f18f512f2ca479fa097e19fa68 ;;
    i486)    digest=sha256:b28a4203553cb48f4b7ff7f4db923967436ac7d6172566bbe2b550e8a6948402 ;;
    *) echo "build-rpm: unknown arch '$arch' (use aarch64, armv7hl or i486)" >&2; exit 2 ;;
esac
image="docker.io/coderus/sailfishos-platform-sdk-$arch:$SFOS_RELEASE@$digest"
target="SailfishOS-$SFOS_RELEASE-$arch"

src=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$outdir"
out=$(cd "$outdir" && pwd)
# The container runs as its own user (mersdk, uid 100000).
chmod a+rwx "$out"

# The files to build: in a git checkout, everything git would commit (tracked
# plus untracked-but-not-ignored, so local build-*/ dirs and test output stay
# out); otherwise the whole tree.
source_files() {
    if git -C "$src" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        git -C "$src" ls-files -z --cached --others --exclude-standard |
            while IFS= read -r -d '' f; do
                [ -e "$src/$f" ] && printf '%s\0' "$f"   # skip deleted files
            done
    else
        (cd "$src" && find . -name .git -prune -o -type f -print0)
    fi
}

echo "build-rpm: $target in $image"
# The sources are streamed into the container as a tar, so the build never
# writes into the checkout and file ownership doesn't matter. mb2 pulls the
# spec's BuildRequires (openssl-devel etc.) from Jolla's repositories into the
# build target by itself.
source_files | tar -C "$src" --null -T - -cf - |
docker run --rm -i \
    -v "$out:/out" \
    -e TARGET="$target" \
    "$image" \
    bash -c '
        set -euo pipefail
        mkdir -p ~/src ~/build
        tar -C ~/src -xf -
        cd ~/build
        mb2 -t "$TARGET" build ~/src
        cp RPMS/*.rpm /out/
        for rpm in RPMS/*.rpm; do
            echo "== $rpm"
            rpm -qp --requires "$rpm" | grep -v "^rpmlib("
        done
    '

ls -l "$out"/*.rpm
