//===-- PIC16F1MCTargetDesc.h - PIC16F1 target descriptions ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PIC16F1_MCTARGETDESC_PIC16F1MCTARGETDESC_H
#define LLVM_LIB_TARGET_PIC16F1_MCTARGETDESC_PIC16F1MCTARGETDESC_H

#include "llvm/Support/DataTypes.h"
#include <memory>

namespace llvm {
class Target;
class MCAsmBackend;
class MCCodeEmitter;
class MCInstrInfo;
class MCSubtargetInfo;
class MCRegisterInfo;
class MCContext;
class MCTargetOptions;
class MCObjectTargetWriter;

MCCodeEmitter *createPIC16F1MCCodeEmitter(const MCInstrInfo &MCII,
                                          MCContext &Ctx);

MCAsmBackend *createPIC16F1MCAsmBackend(const Target &T,
                                        const MCSubtargetInfo &STI,
                                        const MCRegisterInfo &MRI,
                                        const MCTargetOptions &Options);

std::unique_ptr<MCObjectTargetWriter>
createPIC16F1ELFObjectWriter(uint8_t OSABI);

} // namespace llvm

#define GET_REGINFO_ENUM
#include "PIC16F1GenRegisterInfo.inc"

#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "PIC16F1GenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "PIC16F1GenSubtargetInfo.inc"

#endif
