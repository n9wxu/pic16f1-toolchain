//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PIC16F1SelectionDAGInfo.h"

#define GET_SDNODE_DESC
#include "PIC16F1GenSDNodeInfo.inc"

using namespace llvm;

PIC16F1SelectionDAGInfo::PIC16F1SelectionDAGInfo()
    : SelectionDAGGenTargetInfo(PIC16F1GenSDNodeInfo) {}

PIC16F1SelectionDAGInfo::~PIC16F1SelectionDAGInfo() = default;
