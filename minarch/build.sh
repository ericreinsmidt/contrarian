#!/bin/sh
# Build Contrarian's minarch from NextUI source with Contrarian overrides.
#
# We do not fork NextUI: this copies a pristine workspace, overlays only the
# files in overrides/, and builds in the tg5040 toolchain container.
# minarch.elf is GPL-3.0 (NextUI); see THIRD-PARTY-LICENSES.md.
#
# Usage: minarch/build.sh   [NEXTUI_SRC=/path/to/NextUI/workspace]
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NEXTUI_SRC=${NEXTUI_SRC:-$HOME/Projects/NextUI/workspace}
IMAGE=ghcr.io/loveretro/tg5040-toolchain:latest
BUILD=${CTR_MINARCH_BUILD:-/tmp/ctr-minarch/workspace}

[ -d "$NEXTUI_SRC/all/minarch" ] || { echo "NextUI workspace not found at $NEXTUI_SRC"; exit 1; }

echo "copying workspace -> $BUILD"
rm -rf "$BUILD"; mkdir -p "$BUILD"
cp -R "$NEXTUI_SRC/." "$BUILD/"

if [ -d "$ROOT/minarch/overrides" ] && [ -n "$(ls -A "$ROOT/minarch/overrides" 2>/dev/null)" ]; then
	echo "applying Contrarian overrides"
	( cd "$ROOT/minarch/overrides" && cp -R . "$BUILD/" )
fi

docker run --rm -v "$BUILD":/root/workspace "$IMAGE" /bin/bash -c '
	set -e
	source ~/.bashrc 2>/dev/null || true
	cd /root/workspace/tg5040/libmsettings && make
	cd /root/workspace/all/minarch && make PLATFORM=tg5040
'
OUT="$BUILD/all/minarch/build/tg5040/minarch.elf"
[ -f "$OUT" ] || { echo "build failed: no minarch.elf"; exit 1; }
mkdir -p "$ROOT/vendor"
cp "$OUT" "$ROOT/vendor/minarch.elf"
echo "vendor/minarch.elf updated ($(wc -c < "$ROOT/vendor/minarch.elf") bytes)"
