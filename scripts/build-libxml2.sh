#!/bin/sh
# Build libxml2 as a static library with nothing it depends on, for the
# debugger of a release (Linux and Windows). LLDB reads the description of
# the registers that the simulator and a probe send with it; linked in, the
# debugger does not need whichever libxml2 the system has or lacks. (macOS
# always has one, and the build there uses it.)
#
# Usage: scripts/build-libxml2.sh <prefix>
#   CMAKE_EXTRA  extra cmake arguments (the compiler, the run-time library)
# Prints nothing useful; the library is <prefix>/lib/libxml2.a, or
# libxml2s.lib on Windows, with its headers in <prefix>/include/libxml2.
set -eu
prefix=$1
version=2.13.9
sha256=a2c9ae7b770da34860050c309f903221c67830c86e4a7e760692b803df95143a
work=$(dirname "$prefix")/libxml2-src
mkdir -p "$work"
archive=$work/libxml2-$version.tar.xz
[ -f "$archive" ] || curl -sSfL -o "$archive" \
    "https://download.gnome.org/sources/libxml2/${version%.*}/libxml2-$version.tar.xz"
if command -v sha256sum >/dev/null; then got=$(sha256sum "$archive"); else got=$(shasum -a 256 "$archive"); fi
[ "${got%% *}" = "$sha256" ] || { echo "libxml2-$version.tar.xz: wrong checksum" >&2; exit 1; }
[ -d "$work/libxml2-$version" ] || tar -xf "$archive" -C "$work"
# shellcheck disable=SC2086
cmake -G Ninja -S "$work/libxml2-$version" -B "$work/build" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_SHARED_LIBS=OFF \
    -DLIBXML2_WITH_ICONV=OFF -DLIBXML2_WITH_LZMA=OFF -DLIBXML2_WITH_ZLIB=OFF \
    -DLIBXML2_WITH_PYTHON=OFF -DLIBXML2_WITH_HTTP=OFF -DLIBXML2_WITH_FTP=OFF \
    -DLIBXML2_WITH_THREADS=ON -DLIBXML2_WITH_TESTS=OFF -DLIBXML2_WITH_PROGRAMS=OFF \
    -DLIBXML2_WITH_MODULES=OFF ${CMAKE_EXTRA:-}
ninja -C "$work/build" install
echo "libxml2 $version: $prefix"
