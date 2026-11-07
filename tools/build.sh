#!/bin/sh
# Build Survival Radio in the Docker image of the firmware tree, output to builds/survival-radio.bin.
# Usage: [SURVIVAL_VERSION=v1.0] tools/build.sh
# Without Docker: cmake --preset Survival && cmake --build --preset Survival in firmware/
# (ARM GNU toolchain 13.3, CMake, Ninja on the PATH).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/builds"
SURVIVAL_VERSION="${SURVIVAL_VERSION:-v1.0}"
mkdir -p "$OUT"

if command -v colima >/dev/null 2>&1; then
  colima status >/dev/null 2>&1 || colima start
fi
docker build -q -t survival-radio "$ROOT/firmware"

docker run --rm -u "$(id -u):$(id -g)" -v "$ROOT:/src" -w /src/firmware survival-radio /bin/bash -c "\
  rm -rf build/Survival \
  && cmake --preset Survival -DVERSION_STRING_2=$SURVIVAL_VERSION -DSURVIVAL_VERSION=$SURVIVAL_VERSION >/dev/null \
  && cmake --build --preset Survival -j \
  && cp build/Survival/firmware.bin /src/builds/survival-radio.bin"

SIZE=$(wc -c < "$OUT/survival-radio.bin")
echo "bin: $SIZE bytes, free: $((120832 - SIZE)) of 120832"
