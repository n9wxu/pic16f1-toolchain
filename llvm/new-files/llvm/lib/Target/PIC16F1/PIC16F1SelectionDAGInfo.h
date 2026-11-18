//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PIC16F1_PIC16F1SELECTIONDAGINFO_H
#define LLVM_LIB_TARGET_PIC16F1_PIC16F1SELECTIONDAGINFO_H

#include "llvm/CodeGen/SelectionDAGTargetInfo.h"

#define GET_SDNODE_ENUM
#include "PIC16F1GenSDNodeInfo.inc"

namespace llvm {

class PIC16F1SelectionDAGInfo : public SelectionDAGGenTargetInfo {
public:
  PIC16F1SelectionDAGInfo();

  ~PIC16F1SelectionDAGInfo() override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_PIC16F1_PIC16F1SELECTIONDAGINFO_H
