# Regression tests in LLVM's lit/FileCheck style.
#
#   build/llvm-build/bin/llvm-lit tests/lit
#
# The tools come from PIC16F1_LLVM_BIN, or build/llvm-build/bin.
import os

import lit.formats

config.name = "pic16f1"
config.test_format = lit.formats.ShTest(False)
config.suffixes = [".s", ".ll", ".c"]
config.excludes = ["Inputs"]
config.test_source_root = os.path.dirname(__file__)

repo = os.path.dirname(os.path.dirname(config.test_source_root))
tools = os.environ.get("PIC16F1_LLVM_BIN") or os.path.join(repo, "build", "llvm-build", "bin")
config.environment["PATH"] = tools + os.pathsep + config.environment.get("PATH", "")
config.test_exec_root = os.path.join(repo, "build", "lit-out")
config.substitutions.append(("%include", os.path.join(repo, "toolchain", "include")))
config.substitutions.append(("%ldscript", os.path.join(repo, "toolchain", "lib", "pic16f1937.ld")))
