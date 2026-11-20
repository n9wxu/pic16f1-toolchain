#!/bin/sh
# Make the VS Code extension for one system: the extension in vscode/ with
# that system's toolchain archive inside it, as one .vsix file. Installing
# the file installs everything; the extension unpacks the toolchain the
# first time it runs.
#
# Usage: scripts/package-vsix.sh <platform> <toolchain-archive> [out-dir]
#   platform: macos-arm64, macos-x86_64, linux-x86_64, windows-x86_64
#   out-dir defaults to dist
# Needs Node (npx fetches the packaging tool, @vscode/vsce).
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
platform=$1
archive=$2
out=${3:-$root/dist}
version=$(cat "$root/VERSION")
case "$platform" in
macos-arm64)    target=darwin-arm64 ;;
macos-x86_64)   target=darwin-x64 ;;
linux-x86_64)   target=linux-x64 ;;
linux-arm64)    target=linux-arm64 ;;
windows-x86_64) target=win32-x64 ;;
*) echo "package-vsix: unknown platform $platform" >&2; exit 1 ;;
esac
mkdir -p "$out"
out=$(cd "$out" && pwd)
stage=$out/vsix-$platform
[ ! -d "$stage" ] || mv "$stage" "$stage.old.$$"
mkdir -p "$stage/toolchain"
cp "$root/vscode/extension.js" "$root/vscode/README.md" "$root/vscode/CHANGELOG.md" "$stage/"
cp "$root/LICENSE" "$stage/LICENSE"
# The version is the release's.
sed "s/\"version\": \"[^\"]*\"/\"version\": \"$version\"/" "$root/vscode/package.json" > "$stage/package.json"
cp "$archive" "$stage/toolchain/"
echo "$version $platform" > "$stage/toolchain/VERSION"
(cd "$stage" && npx --yes @vscode/vsce@3 package --target "$target" --no-dependencies \
    --allow-missing-repository -o "$out/pic16f1-toolchain-$version-$platform.vsix")
echo "$out/pic16f1-toolchain-$version-$platform.vsix"
