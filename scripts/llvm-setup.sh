#!/bin/sh
# Fetch the pinned LLVM release and apply the PIC16F1 changes to it.
# The result is a normal llvm-project checkout in build/llvm-project.
#
# Usage: scripts/llvm-setup.sh [dest-dir]
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
dest=${1:-$root/build/llvm-project}
tag=$(cat "$root/llvm/LLVM_VERSION")
if [ -d "$dest/.git" ]; then
    echo "$dest already exists; remove it to start again" >&2
    exit 1
fi
mkdir -p "$(dirname "$dest")"
git clone --quiet --depth 1 --branch "$tag" https://github.com/llvm/llvm-project "$dest"
git -C "$dest" apply "$root/llvm/existing-files.patch"
cp -R "$root/llvm/new-files/." "$dest/"
echo "LLVM $tag with PIC16F1 changes is in $dest"
