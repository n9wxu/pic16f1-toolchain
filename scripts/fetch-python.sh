#!/bin/sh
# Fetch the Python that a release for one system carries (the toolchain's
# drivers are Python scripts): a build from python-build-standalone, pinned
# here by release and checksum.
#
# Usage: scripts/fetch-python.sh <platform> <directory>
#   platform: macos-arm64, macos-x86_64, linux-x86_64, linux-arm64, windows-x86_64
# Prints the path of the archive.
set -eu
platform=$1
dir=$2
release=20261110
python=3.12.16
case "$platform" in
macos-arm64)    triple=aarch64-apple-darwin;      sha256=2f11b8aa3726bdbd87dd9eb76568412faddf4f47e26913fa45d6a2e424972acd ;;
macos-x86_64)   triple=x86_64-apple-darwin;       sha256=b78a4c479f1964ec484880bee567568266f68da102269fb93a05fe8716b45f91 ;;
linux-x86_64)   triple=x86_64-unknown-linux-gnu;  sha256=6a9e62dad033790aef722dbd370e0ce76ce17b73627ba3ae679f537049350b39 ;;
linux-arm64)    triple=aarch64-unknown-linux-gnu; sha256=b6a41defec4975d94b4ff1950236e7383e5c02ba40ea48c9ec33c66087a35aab ;;
windows-x86_64) triple=x86_64-pc-windows-msvc;    sha256=355ee61b8207b91574491b1c6dc9b0ebb4408fc56bfafe498f3ef439314b52dc ;;
*) echo "fetch-python: no Python pinned for $platform" >&2; exit 1 ;;
esac
name=cpython-$python+$release-$triple-install_only_stripped.tar.gz
mkdir -p "$dir"
out=$dir/$name
[ -f "$out" ] || curl -sSfL -o "$out" \
    "https://github.com/astral-sh/python-build-standalone/releases/download/$release/cpython-$python%2B$release-$triple-install_only_stripped.tar.gz"
if command -v sha256sum >/dev/null; then got=$(sha256sum "$out"); else got=$(shasum -a 256 "$out"); fi
[ "${got%% *}" = "$sha256" ] || { echo "fetch-python: $name: wrong checksum" >&2; exit 1; }
echo "$out"
