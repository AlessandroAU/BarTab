#!/bin/sh
# Configure, compile, run the tests and install to bin/, as build.bat does on Windows.
# Usage: ./build.sh [Release|Debug]
set -eu
configuration="${1:-Release}"
case "$configuration" in
Release | Debug) ;;
*)
    echo "Usage: ./build.sh [Release|Debug]" >&2
    exit 1
    ;;
esac
root="$(cd "$(dirname "$0")" && pwd)"
command -v cmake >/dev/null 2>&1 || {
    echo "Install CMake 3.22 or newer." >&2
    exit 1
}
generator=""
command -v ninja >/dev/null 2>&1 && generator="-G Ninja"
# shellcheck disable=SC2086 # the generator is one optional word pair
cmake -S "$root" -B "$root/build" $generator -DCMAKE_BUILD_TYPE="$configuration" || {
    echo "CMake configure failed. On Debian/Ubuntu: sudo apt install build-essential cmake libx11-dev" \
        "libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl-dev libfontconfig-dev" \
        "libglib2.0-dev" >&2
    exit 1
}
# Stop installed and build-tree instances so their executables can be replaced.
# Match the executable path: process names are cut to 15 characters.
pkill -f '/BarTab( |$)' 2>/dev/null || true
pkill -f '/BarTabDebug( |$)' 2>/dev/null || true
cmake --build "$root/build" --parallel
ctest --test-dir "$root/build" --output-on-failure
cmake --install "$root/build" --prefix "$root/bin"
echo "Built and tested $root/bin/BarTab and the mock-provider BarTabDebug"
