#!/usr/bin/env bash
# Cross-compile libforce_cursor.so and probe_inputs for the Akai Force and stage the
# addon folder in dist/mouseCursor (plus dist/force-mouse-<version>-armv7.zip).
#
#   tools/build.sh          build with Docker (builds the toolchain image on first use)
#   tools/build.sh --inside run the compile step directly (used inside the container)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${FORCE_MOUSE_BUILDER:-force-mouse-builder:bookworm}"
MAX_GLIBC="2.36" # the Force runs 2.39; anything newer than the toolchain's would not load

compile() {
    cd "$ROOT"
    mkdir -p build
    local cflags="-O2 -Wall -Wextra -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard"
    arm-linux-gnueabihf-gcc -shared -fPIC $cflags \
        -I/usr/include/libdrm -I/usr/include/arm-linux-gnueabihf \
        -o build/libforce_cursor.so src/force_cursor.c -ldl -lpthread -lasound -ldrm
    for tool in probe_inputs evdump fake_mouse; do
        arm-linux-gnueabihf-gcc $cflags -o "build/$tool" "tools/$tool.c"
    done
    for tool in drm_planes drm_screenshot; do
        arm-linux-gnueabihf-gcc $cflags -I/usr/include/libdrm -I/usr/include/arm-linux-gnueabihf \
            -o "build/$tool" "tools/$tool.c" -ldrm
    done
    arm-linux-gnueabihf-strip build/libforce_cursor.so build/probe_inputs build/evdump build/fake_mouse build/drm_planes build/drm_screenshot

    echo "--- checks"
    for f in build/libforce_cursor.so build/probe_inputs build/evdump build/fake_mouse build/drm_planes build/drm_screenshot; do
        arm-linux-gnueabihf-readelf -h "$f" | grep -E 'Class:|Machine:' | tr -s ' ' | sed "s|^|$f: |"
        arm-linux-gnueabihf-readelf -d "$f" | grep NEEDED | sed "s|^|$f: |"
        local highest
        highest="$(arm-linux-gnueabihf-readelf -V "$f" | grep -o 'GLIBC_[0-9.]*' | sort -V | tail -n 1 | sed 's/GLIBC_//')"
        echo "$f: highest glibc symbol ${highest:-none}"
        if [ -n "$highest" ] && [ "$(printf '%s\n%s\n' "$highest" "$MAX_GLIBC" | sort -V | tail -n 1)" != "$MAX_GLIBC" ]; then
            echo "ERROR: $f needs glibc $highest (> $MAX_GLIBC)" >&2
            exit 1
        fi
    done
}

stage() {
    cd "$ROOT"
    local version dest
    version="$(tr -d '[:space:]' <addon/VERSION)"
    dest="dist/mouseCursor"
    rm -rf "$dest"
    mkdir -p "$dest"
    cp addon/manage.sh addon/run_mouseCursor.sh addon/device.txt addon/README.txt addon/VERSION "$dest/"
    cp LICENSE NOTICE.md "$dest/"
    cp build/libforce_cursor.so "$dest/"
    mkdir -p "$dest/tools"
    cp build/probe_inputs build/evdump build/fake_mouse build/drm_planes build/drm_screenshot "$dest/tools/"
    cp tools/touch_reset.sh "$dest/tools/"
    chmod +x "$dest/manage.sh" "$dest/run_mouseCursor.sh" "$dest"/tools/*
    rm -f "dist/force-mouse-${version}-armv7.zip"
    (cd dist && zip -qr "force-mouse-${version}-armv7.zip" mouseCursor)
    (cd dist && sha256sum "force-mouse-${version}-armv7.zip" mouseCursor/libforce_cursor.so | tee SHA256SUMS)
    echo "staged: dist/mouseCursor and dist/force-mouse-${version}-armv7.zip"
}

if [ "${1:-}" = "--inside" ]; then
    compile
    exit 0
fi

command -v docker >/dev/null || { echo "docker is required" >&2; exit 1; }
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "building toolchain image $IMAGE ..."
    docker build -t "$IMAGE" -f "$ROOT/tools/Dockerfile" "$ROOT/tools"
fi
docker run --rm --user "$(id -u):$(id -g)" -v "$ROOT:/work" -w /work "$IMAGE" tools/build.sh --inside
stage
