#!/bin/sh
# Make the macOS packages of a release, on a Mac: for Apple silicon and
# for Intel, the toolchain archive and the VS Code extension that carries
# it, in dist/. (Linux and Windows are made by the Toolchain workflow;
# macOS is not, because a hosted macOS runner costs ten times a Linux one.)
#
# The programs are built apart from the checkout's own build
# (build/release/macos-ARCH), for macOS 11 and later. The files for the
# PIC16F1 (headers, libraries, templates) are those of TARGET_FILES, the
# pic16f1-target-files.tar.gz that the Toolchain workflow made and tested,
# so that a release has the same ones on every system. Without it they
# come from this checkout's build/toolchain, brought up to date first;
# that is for trying the packaging, not for a release.
#
# Usage: scripts/release-macos.sh [arm64] [x86_64]      (both if none named)
#   TARGET_FILES  the workflow's pic16f1-target-files.tar.gz
#   NINJA_JOBS    compilations at once for the LLVM builds (default 5)
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
version=$(cat "$root/VERSION")
[ $# -gt 0 ] || set -- arm64 x86_64
export MACOSX_DEPLOYMENT_TARGET=11.0
if [ -n "${TARGET_FILES:-}" ]; then
    target=$root/build/release/target
    [ ! -d "$target" ] || mv "$target" "$target.old.$$"
    mkdir -p "$target"
    tar -xzf "$TARGET_FILES" -C "$target"
    target=$target/pic16f1-target-files
else
    echo "== no TARGET_FILES: the PIC16F1 files of this checkout (not for a release)"
    if pgrep -x lldb-dap > /dev/null; then
        echo "release-macos: a debug session is using build/toolchain; end it first" >&2
        exit 1
    fi
    "$root/scripts/build-runtime.sh" > /dev/null
    "$root/scripts/package-toolchain.sh" > /dev/null
    target=$root/build/toolchain
fi
for arch in "$@"; do
    case "$arch" in
    arm64)  rust=aarch64-apple-darwin; triple= ;;
    x86_64) rust=x86_64-apple-darwin;  triple=-DLLVM_HOST_TRIPLE=x86_64-apple-darwin ;;
    *) echo "release-macos: arm64 or x86_64, not $arch" >&2; exit 1 ;;
    esac
    platform=macos-$arch
    work=$root/build/release/$platform
    echo "== $platform: the compiler and the debugger"
    HOST_ONLY=1 NINJA_JOBS=${NINJA_JOBS:-5} LLVM_BUILD=$work/llvm \
        CMAKE_EXTRA="-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DCMAKE_OSX_ARCHITECTURES=$arch $triple" \
        "$root/scripts/build-llvm.sh" > "$work.llvm.log" 2>&1 || { tail -20 "$work.llvm.log"; exit 1; }
    echo "== $platform: the simulator"
    cargo build --quiet --release --locked --manifest-path "$root/sim/Cargo.toml" \
        --target "$rust" --target-dir "$work/sim"
    echo "== $platform: the package"
    python=$("$root/scripts/fetch-python.sh" "$platform" "$root/build/release/dl")
    python3 "$root/tools/package_release.py" --platform "$platform" --version "$version" \
        --llvm-build "$work/llvm" --sim "$work/sim/$rust/release/pic16f1-sim" \
        --target-tree "$target" --python "$python" --out "$root/dist"
    # As someone with nothing installed would use it. (The Intel programs
    # run on Apple silicon through Rosetta.)
    env -i HOME="$HOME" PATH=/usr/bin:/bin "$root/tools/test_package.sh" "$root/dist/$platform/pic16f1-toolchain"
    "$root/scripts/package-vsix.sh" "$platform" "$root/dist/pic16f1-toolchain-$version-$platform.tar.gz" "$root/dist"
done
ls -l "$root"/dist/*macos*
