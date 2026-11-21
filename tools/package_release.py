#!/usr/bin/env python3
"""Make the toolchain tree of a release for one system, and its archive.

    tools/package_release.py --platform macos-arm64 --version 0.2.0
        --llvm-build build/release/macos-arm64/llvm
        --sim build/release/macos-arm64/sim/release/pic16f1-sim
        --target-tree build/toolchain --python cpython-...tar.gz
        --out dist

A release is the same files for every system except the programs that run
on it. Those come from --llvm-build (clang, the linker, the LLVM tools, the
debugger) and --sim; everything for the PIC16F1 (headers, runtime library,
linker scripts, picolibc, libc++, templates) comes from --target-tree, a
toolchain tree that has them (build/toolchain, or the unpacked
pic16f1-target-files archive that --target-files-out wrote on another
system). --python is a python-build-standalone "install_only" archive for
the system: the drivers are Python, and the package brings its own.
--launcher is the Windows program that runs a driver (toolchain/windows).

The result is OUT/PLATFORM/pic16f1-toolchain and
OUT/pic16f1-toolchain-VERSION-PLATFORM.tar.gz (.zip for Windows). The tree
has no symbolic links, so that it survives every kind of archive.
"""
import argparse
import os
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# The LLVM programs a toolchain has, by the name each is run as.
LLVM_TOOLS = ["clang", "ld.lld", "llvm-mc", "llvm-objdump", "llvm-readelf", "llvm-objcopy", "llvm-ar", "llvm-nm",
              "llvm-size", "lldb", "lldb-dap"]
DRIVERS = ["pic16f1-cc", "pic16f1-build", "pic16f1-arduino-ide", "pic16f1-forth", "pic16f1-new-project", "pic16f1-retarget",
           "pic16f1-rust-ld", "pic16f1-zig"]
# What of a toolchain tree is the same on every system.
TARGET_PARTS = ["include", "lib/pic16f1", "lib/clang", "picolibc", "share"]


def copy_tree(src, dst):
    shutil.copytree(src, dst, symlinks=False, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("__pycache__", "*.pyc", ".DS_Store"))


def executable(path):
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--platform", required=True, help="macos-arm64, macos-x86_64, linux-x86_64, windows-x86_64")
    ap.add_argument("--version", required=True)
    ap.add_argument("--llvm-build", required=True)
    ap.add_argument("--sim", required=True)
    ap.add_argument("--target-tree", required=True)
    ap.add_argument("--python")
    ap.add_argument("--launcher")
    ap.add_argument("--out", default=os.path.join(ROOT, "dist"))
    ap.add_argument("--target-files-out", help="also write the parts that are the same on every system here")
    ap.add_argument("--no-strip", action="store_true")
    o = ap.parse_args()
    windows = o.platform.startswith("windows")
    mac = o.platform.startswith("macos")
    exe = ".exe" if windows else ""

    tree = os.path.join(o.out, o.platform, "pic16f1-toolchain")
    shutil.rmtree(tree, ignore_errors=True)
    os.makedirs(os.path.join(tree, "bin"))
    os.makedirs(os.path.join(tree, "lib"))

    # The programs. (A build names some of them by links: clang is
    # clang-23, ld.lld is lld. The copy follows the link.)
    binaries = []
    for t in LLVM_TOOLS:
        src = os.path.join(o.llvm_build, "bin", t + exe)
        if not os.path.exists(src):
            sys.exit(f"package_release: no {src}")
        dst = os.path.join(tree, "bin", t + exe)
        shutil.copyfile(os.path.realpath(src), dst)
        binaries.append(dst)
    dst = os.path.join(tree, "bin", "pic16f1-sim" + exe)
    shutil.copyfile(o.sim, dst)
    binaries.append(dst)
    # The debugger's library, under the name the programs ask for.
    libdir = os.path.join(o.llvm_build, "bin" if windows else "lib")
    wanted = (r"liblldb\.dll$" if windows else r"liblldb\.\d+\.\d+\.dylib$" if mac else r"liblldb\.so\.\d+\.\d+$")
    found = [f for f in os.listdir(libdir) if re.match(wanted, f)]
    if len(found) != 1:
        sys.exit(f"package_release: expected one LLDB library in {libdir}, found {found}")
    dst = os.path.join(tree, "bin" if windows else "lib", found[0])
    shutil.copyfile(os.path.realpath(os.path.join(libdir, found[0])), dst)
    binaries.append(dst)
    for b in binaries:
        executable(b)
    if not o.no_strip and not windows:
        for b in binaries:
            subprocess.run(["strip", "-x", b] if mac else ["strip", b], check=True, stderr=subprocess.DEVNULL)
            if mac:
                # Stripping breaks the signature the linker made, and
                # macOS on Apple silicon runs nothing unsigned.
                subprocess.run(["codesign", "--force", "--sign", "-", b], check=True, capture_output=True)

    # The drivers, and how each is started.
    for d in DRIVERS:
        dst = os.path.join(tree, "bin", d)
        shutil.copyfile(os.path.join(ROOT, "toolchain", "bin", d), dst)
        executable(dst)
        if windows:
            if not o.launcher:
                sys.exit("package_release: Windows needs --launcher")
            shutil.copyfile(o.launcher, dst + ".exe")
    if windows:
        for name, program, extra in (("pic16f1-lldb", "lldb.exe",
                                      ' -O "settings set target.process.memory-cache-line-size 32"'),
                                     ("pic16f1-lldb-dap", "lldb-dap.exe", "")):
            with open(os.path.join(tree, "bin", name + ".cmd"), "w", newline="\r\n") as f:
                f.write(f'@"%~dp0{program}"{extra} %*\n')
    else:
        for name in ("pic16f1-lldb", "pic16f1-lldb-dap"):
            dst = os.path.join(tree, "bin", name)
            shutil.copyfile(os.path.join(ROOT, "toolchain", "bin", name), dst)
            executable(dst)

    # What is the same everywhere.
    for part in TARGET_PARTS:
        src = os.path.join(o.target_tree, part)
        if not os.path.isdir(src):
            sys.exit(f"package_release: {o.target_tree} has no {part}; is it a complete toolchain tree "
                     "(runtime library, picolibc, libc++)?")
        copy_tree(src, os.path.join(tree, part))
    for needed in ("lib/pic16f1/libpicrt.a", "lib/pic16f1/libc++.a", "include/c++/vector",
                   "include/pic16f1/pic16f1937.h", "picolibc/banks32/include/stdio.h", "share/pic16f1/project/main.c"):
        if not os.path.exists(os.path.join(tree, needed)):
            sys.exit(f"package_release: {o.target_tree} lacks {needed}")
    if o.target_files_out:
        os.makedirs(os.path.dirname(os.path.abspath(o.target_files_out)), exist_ok=True)
        with tarfile.open(o.target_files_out, "w:gz") as tar:
            for part in TARGET_PARTS:
                tar.add(os.path.join(tree, part), arcname="pic16f1-target-files/" + part)

    # Python for the drivers.
    if o.python:
        libexec = os.path.join(tree, "libexec")
        os.makedirs(libexec)
        with tarfile.open(o.python) as tar:
            tar.extractall(libexec, filter="tar") if hasattr(tarfile, "tar_filter") else tar.extractall(libexec)
        python = os.path.join(libexec, "python", "python.exe" if windows else "bin/python3")
        if not os.path.exists(python):
            sys.exit(f"package_release: {o.python} has no {os.path.relpath(python, libexec)}")
        # Links inside it become copies, as everywhere in this tree.
        for folder, dirs, files in os.walk(libexec):
            for f in files + dirs:
                p = os.path.join(folder, f)
                if os.path.islink(p):
                    target = os.path.realpath(p)
                    os.unlink(p)
                    if os.path.isdir(target):
                        shutil.copytree(target, p)
                    else:
                        shutil.copy2(target, p)
        # What a compiler driver never uses.
        for extra in ("share", "include", "lib/python3.12/test", "lib/python3.12/idlelib", "lib/python3.12/tkinter",
                      "lib/python3.12/ensurepip", "lib/python3.12/site-packages", "lib/tcl8.6", "lib/tk8.6",
                      "lib/itcl4.2.4", "lib/thread2.8.9", "lib/tcl8", "Lib/test", "Lib/idlelib", "Lib/tkinter",
                      "Lib/ensurepip", "Lib/site-packages", "tcl", "Scripts", "libs"):
            shutil.rmtree(os.path.join(libexec, "python", extra), ignore_errors=True)
        home = os.path.join(libexec, "python")
        for folder, dirs, files in os.walk(home):
            for d in [d for d in dirs if re.match(r"(tcl|tk|itcl|thread)\d", d)]:
                shutil.rmtree(os.path.join(folder, d), ignore_errors=True)
                dirs.remove(d)
            for f in files:
                if re.search(r"(^lib(tcl|tk)|_tkinter|^tcl\d|^tk\d)", f):
                    os.unlink(os.path.join(folder, f))
        if not windows:
            # One python3, not the same program under three names, and
            # none of the commands that come with it.
            for f in os.listdir(os.path.join(home, "bin")):
                if f != "python3":
                    os.unlink(os.path.join(home, "bin", f))

    with open(os.path.join(tree, "VERSION"), "w") as f:
        f.write(f"{o.version} {o.platform}\n")
    shutil.copyfile(os.path.join(ROOT, "LICENSE"), os.path.join(tree, "LICENSE"))
    notices = os.path.join(ROOT, "toolchain", "THIRD-PARTY.md")
    if os.path.exists(notices):
        shutil.copyfile(notices, os.path.join(tree, "THIRD-PARTY.md"))

    base = os.path.join(o.out, f"pic16f1-toolchain-{o.version}-{o.platform}")
    if windows:
        archive = base + ".zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for folder, _, files in os.walk(tree):
                for f in files:
                    p = os.path.join(folder, f)
                    z.write(p, os.path.join("pic16f1-toolchain", os.path.relpath(p, tree)))
    else:
        archive = base + ".tar.gz"
        with tarfile.open(archive, "w:gz") as tar:
            tar.add(tree, arcname="pic16f1-toolchain")
    print(f"{tree}\n{archive}: {os.path.getsize(archive) / 1e6:.0f} MB")


if __name__ == "__main__":
    main()
