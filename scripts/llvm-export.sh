#!/bin/sh
# Save the LLVM working tree's changes into this repository:
#   llvm/existing-files.patch   edits to files that exist upstream
#   llvm/new-files/             files added for the PIC16F1 target
# Run after changing anything under build/llvm-project.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
src=${LLVM_SRC:-$root/build/llvm-project}
cd "$src"
git diff --no-color > "$root/llvm/existing-files.patch"
rm -rf "$root/llvm/new-files"
git ls-files --others --exclude-standard | while read -r f; do
    mkdir -p "$root/llvm/new-files/$(dirname "$f")"
    cp "$f" "$root/llvm/new-files/$f"
done
echo "patch: $(grep -c '^diff --git' "$root/llvm/existing-files.patch") files changed"
echo "new files: $(find "$root/llvm/new-files" -type f | wc -l | tr -d ' ')"
