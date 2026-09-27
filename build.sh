#!/usr/bin/env bash
#
# Build the native half of r8 One: QDOS's core and the Quadrate libraries it
# links, from the submodules under external/. qmake links them into the app.
#
#   ./build.sh
#
# Run inside the Sailfish SDK build engine, where the RPM's %build step calls
# it; the compiler there already targets the phone, so there is no cross file.
# Nothing is downloaded: every source is a submodule. Run
# `git submodule update --init` first.
#
# Output: build/native/lib/, which harbour-r8-one.pro links.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
QDOS_DIR="$ROOT/external/qdos"
QUADRATE_DIR="$ROOT/external/quadrate"
U8T_DIR="$ROOT/external/libu8t"
OUT=${R8_BUILD_DIR:-$ROOT/build/native}

for dir in "$QDOS_DIR" "$QUADRATE_DIR" "$U8T_DIR"; do
	if [ ! -f "$dir/README.md" ]; then
		echo "$0: $dir is empty; run git submodule update --init" >&2
		exit 1
	fi
done

# Quadrate names u8t as a meson wrap, which would clone it. It is placed where
# the wrap would have put it instead, with the build file Quadrate overlays on
# it; the path is in Quadrate's .gitignore, so the submodule stays clean.
rm -rf "$QUADRATE_DIR/subprojects/u8t"
cp -r "$U8T_DIR" "$QUADRATE_DIR/subprojects/u8t"
rm -rf "$QUADRATE_DIR/subprojects/u8t/.git"
cp -r "$QUADRATE_DIR/subprojects/packagefiles/u8t/." "$QUADRATE_DIR/subprojects/u8t/"

mkdir -p "$OUT"

# Reconfigured when it is already there, so option changes are picked up; set
# up afresh otherwise
meson_setup() {
	local build="$1"
	shift
	if [ -d "$build/meson-private" ]; then
		meson setup "$build" "$@" --reconfigure >/dev/null 2>&1 || meson setup "$build" "$@" --wipe
	else
		meson setup "$build" "$@"
	fi
}

echo "--- Quadrate"
# Only math of the standard library is linked, and the others would want
# OpenSSL and friends
quadrate_opts=(
	--buildtype=release
	--wrap-mode=nodownload
	-Dbuild_tests=false
	-Dbuild_tools=false
	-Dbuild_compiler=false
	-Dstdlib_modules=math
	-Dwerror=false
)
meson_setup "$OUT/quadrate" "$QUADRATE_DIR" "${quadrate_opts[@]}"
meson compile -C "$OUT/quadrate" interp qc math rt_static u8t

# Meson's are thin archives, which point into the build tree, so each is
# repacked whole wherever it is wanted
repack() {
	local archive="$1" to="$2"
	local members
	members=$(ar t "$archive")
	rm -f "$to"
	(cd "$(dirname "$archive")" && ar rcs "$to" $members)
}

# Staged under the names and layout qdos's meson.build links against
stage="$OUT/quadrate-dist"
rm -rf "$stage"
mkdir -p "$stage/lib/quadrate" "$stage/include/quadrate"
repack "$OUT/quadrate/lib/interp/libinterp.a" "$stage/lib/quadrate/libinterp.a"
repack "$OUT/quadrate/lib/qc/libqc.a" "$stage/lib/quadrate/libqc.a"
repack "$OUT/quadrate/lib/rt/librt_static.a" "$stage/lib/quadrate/librt.a"
repack "$OUT/quadrate/stdlib/math/libmath.a" "$stage/lib/quadrate/libmath.a"
repack "$OUT/quadrate/subprojects/u8t/libu8t.a" "$stage/lib/quadrate/libu8t.a"
for mod in lib/interp lib/qc lib/rt stdlib/math; do
	cp -r "$QUADRATE_DIR/$mod/include/quadrate/$(basename "$mod")" "$stage/include/quadrate/"
done

echo "--- QDOS"
# Only the core: the shell, the display and the keypad drawing. The backend is
# this app's own (src/machine.cpp), so neither of QDOS's is built; the device
# one is switched on only because meson wants one of the two named.
qdos_opts=(
	--buildtype=release
	-Dsim=false
	-Ddevice=true
	-Dquadrate_src="$QUADRATE_DIR"
	-Dquadrate_dist="$stage"
)
meson_setup "$OUT/qdos" "$QDOS_DIR" "${qdos_opts[@]}"
meson compile -C "$OUT/qdos" qdos_core

mkdir -p "$OUT/lib"
repack "$OUT/qdos/libqdos_core.a" "$OUT/lib/libqdos_core.a"
for name in interp qc math rt u8t; do
	cp "$stage/lib/quadrate/lib$name.a" "$OUT/lib/"
done
