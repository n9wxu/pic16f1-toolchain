//===--- PIC16F1.cpp - Implement PIC16F1 target feature support -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PIC16F1.h"
#include "clang/Basic/MacroBuilder.h"
#include "llvm/ADT/StringExtras.h"

using namespace clang;
using namespace clang::targets;

void PIC16F1TargetInfo::getTargetDefines(const LangOptions &Opts,
                                         MacroBuilder &Builder) const {
  Builder.defineMacro("__PIC16F1__");
  Builder.defineMacro("__pic16f1__");
  // -mcpu=pic16f1937 defines __PIC16F1937__ and, as XC8 does, _16F1937.
  if (StringRef(CPU).starts_with("pic")) {
    Builder.defineMacro("__" + StringRef(CPU).upper() + "__");
    Builder.defineMacro("_" + StringRef(CPU).substr(3).upper());
  }
}
