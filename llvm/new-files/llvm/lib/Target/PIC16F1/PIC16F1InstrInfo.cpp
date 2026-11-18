//===-- PIC16F1InstrInfo.cpp - Instruction information --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PIC16F1InstrInfo.h"
#include "PIC16F1.h"
#include "PIC16F1Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "PIC16F1GenInstrInfo.inc"

void PIC16F1InstrInfo::anchor() {}

PIC16F1InstrInfo::PIC16F1InstrInfo(const PIC16F1Subtarget &STI)
    : PIC16F1GenInstrInfo(STI, RI, PIC16F1::ADJCALLSTACKDOWN,
                          PIC16F1::ADJCALLSTACKUP),
      RI() {}

void PIC16F1InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator I,
                                   const DebugLoc &DL, Register DestReg,
                                   Register SrcReg, bool KillSrc,
                                   bool RenamableDest, bool RenamableSrc) const {
  unsigned Opc;
  // The stack pointer is FSR1. An interrupt handler has its own stack, so
  // FSR1 may be read or written a byte at a time.
  if (DestReg == PIC16F1::SP || SrcReg == PIC16F1::SP) {
    bool ToSP = DestReg == PIC16F1::SP;
    unsigned Pair = fileAddr(ToSP ? SrcReg : DestReg);
    for (unsigned B = 0; B != 2; ++B) {
      unsigned From = ToSP ? Pair + B : PIC16F1::FSR1L_ADDR + B;
      unsigned To = ToSP ? PIC16F1::FSR1L_ADDR + B : Pair + B;
      BuildMI(MBB, I, DL, get(PIC16F1::MOVF)).addImm(From).addImm(0);
      BuildMI(MBB, I, DL, get(PIC16F1::MOVWF)).addImm(To);
    }
    return;
  }
  if (PIC16F1::GPR16RegClass.contains(DestReg, SrcReg))
    Opc = PIC16F1::MOV16rr;
  else if (PIC16F1::GPR8RegClass.contains(DestReg, SrcReg))
    Opc = PIC16F1::MOV8rr;
  else
    llvm_unreachable("impossible register copy");
  BuildMI(MBB, I, DL, get(Opc), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
}

void PIC16F1InstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
    bool isKill, int FrameIdx, const TargetRegisterClass *RC, Register VReg,
    MachineInstr::MIFlag Flags) const {
  DebugLoc DL;
  if (MI != MBB.end())
    DL = MI->getDebugLoc();
  unsigned Opc = RI.getRegSizeInBits(*RC) == 16 ? PIC16F1::ST16fi
                                                : PIC16F1::ST8fi;
  BuildMI(MBB, MI, DL, get(Opc))
      .addReg(SrcReg, getKillRegState(isKill))
      .addFrameIndex(FrameIdx)
      .addImm(0);
}

void PIC16F1InstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                            MachineBasicBlock::iterator MI,
                                            Register DestReg, int FrameIdx,
                                            const TargetRegisterClass *RC,
                                            Register VReg, unsigned SubReg,
                                            MachineInstr::MIFlag Flags) const {
  DebugLoc DL;
  if (MI != MBB.end())
    DL = MI->getDebugLoc();
  unsigned Opc = RI.getRegSizeInBits(*RC) == 16 ? PIC16F1::LD16fi
                                                : PIC16F1::LD8fi;
  BuildMI(MBB, MI, DL, get(Opc), DestReg).addFrameIndex(FrameIdx).addImm(0);
}

Register PIC16F1InstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                               int &FrameIndex) const {
  unsigned Opc = MI.getOpcode();
  if ((Opc == PIC16F1::LD8fi || Opc == PIC16F1::LD16fi) &&
      MI.getOperand(1).isFI() && MI.getOperand(2).isImm() &&
      MI.getOperand(2).getImm() == 0) {
    FrameIndex = MI.getOperand(1).getIndex();
    return MI.getOperand(0).getReg();
  }
  return Register();
}

Register PIC16F1InstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                              int &FrameIndex) const {
  unsigned Opc = MI.getOpcode();
  if ((Opc == PIC16F1::ST8fi || Opc == PIC16F1::ST16fi) &&
      MI.getOperand(1).isFI() && MI.getOperand(2).isImm() &&
      MI.getOperand(2).getImm() == 0) {
    FrameIndex = MI.getOperand(1).getIndex();
    return MI.getOperand(0).getReg();
  }
  return Register();
}

//===----------------------------------------------------------------------===//
// Branches
//===----------------------------------------------------------------------===//

bool PIC16F1InstrInfo::isCompareBranch(unsigned Opc) {
  return Opc == PIC16F1::BRCC8rr || Opc == PIC16F1::BRCC8ri ||
         Opc == PIC16F1::BRCC16rr || Opc == PIC16F1::BRCC16ri;
}

static bool isUncondBranch(unsigned Opc) {
  return Opc == PIC16F1::JMP || Opc == PIC16F1::JMPFAR;
}

bool PIC16F1InstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 4 && "invalid branch condition");
  Cond[1].setImm(Cond[1].getImm() ^ 1);
  return false;
}

bool PIC16F1InstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                     MachineBasicBlock *&TBB,
                                     MachineBasicBlock *&FBB,
                                     SmallVectorImpl<MachineOperand> &Cond,
                                     bool AllowModify) const {
  // Condition layout: opcode, condition code, left operand, right operand.
  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end() || !isUnpredicatedTerminator(*I))
    return false;

  MachineInstr *Last = &*I;
  MachineInstr *Prev = nullptr;
  if (I != MBB.begin()) {
    --I;
    while (I->isDebugInstr() && I != MBB.begin())
      --I;
    if (!I->isDebugInstr() && isUnpredicatedTerminator(*I)) {
      Prev = &*I;
      // More than two terminators is not a shape we produce.
      if (I != MBB.begin()) {
        --I;
        while (I->isDebugInstr() && I != MBB.begin())
          --I;
        if (!I->isDebugInstr() && isUnpredicatedTerminator(*I))
          return true;
      }
    }
  }

  auto SetCond = [&](MachineInstr &MI) {
    Cond.push_back(MachineOperand::CreateImm(MI.getOpcode()));
    Cond.push_back(MI.getOperand(0));
    Cond.push_back(MI.getOperand(1));
    Cond.push_back(MI.getOperand(2));
  };

  if (!Prev) {
    if (isUncondBranch(Last->getOpcode())) {
      TBB = Last->getOperand(0).getMBB();
      return false;
    }
    if (isCompareBranch(Last->getOpcode())) {
      TBB = Last->getOperand(3).getMBB();
      SetCond(*Last);
      return false;
    }
    return true;
  }
  if (isCompareBranch(Prev->getOpcode()) && isUncondBranch(Last->getOpcode())) {
    TBB = Prev->getOperand(3).getMBB();
    FBB = Last->getOperand(0).getMBB();
    SetCond(*Prev);
    return false;
  }
  return true;
}

unsigned PIC16F1InstrInfo::removeBranch(MachineBasicBlock &MBB,
                                        int *BytesRemoved) const {
  if (BytesRemoved)
    *BytesRemoved = 0;
  unsigned Count = 0;
  MachineBasicBlock::iterator I = MBB.end();
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (!isUncondBranch(I->getOpcode()) && !isCompareBranch(I->getOpcode()))
      break;
    if (BytesRemoved)
      *BytesRemoved += getInstSizeInBytes(*I);
    I->eraseFromParent();
    I = MBB.end();
    ++Count;
  }
  return Count;
}

unsigned PIC16F1InstrInfo::insertBranch(MachineBasicBlock &MBB,
                                        MachineBasicBlock *TBB,
                                        MachineBasicBlock *FBB,
                                        ArrayRef<MachineOperand> Cond,
                                        const DebugLoc &DL,
                                        int *BytesAdded) const {
  assert(TBB && "insertBranch must not be told to insert a fallthrough");
  if (BytesAdded)
    *BytesAdded = 0;
  unsigned Count = 0;
  auto Added = [&](MachineInstr *MI) {
    ++Count;
    if (BytesAdded)
      *BytesAdded += getInstSizeInBytes(*MI);
  };
  if (Cond.empty()) {
    assert(!FBB && "unconditional branch with two successors");
    Added(BuildMI(&MBB, DL, get(PIC16F1::JMP)).addMBB(TBB));
    return Count;
  }
  assert(Cond.size() == 4 && "invalid branch condition");
  MachineInstrBuilder MIB = BuildMI(&MBB, DL, get(Cond[0].getImm()));
  for (unsigned I = 1; I != 4; ++I) {
    MachineOperand MO = Cond[I];
    if (MO.isReg())
      MO.setIsKill(false);
    MIB.add(MO);
  }
  MIB.addMBB(TBB);
  Added(MIB);
  if (FBB)
    Added(BuildMI(&MBB, DL, get(PIC16F1::JMP)).addMBB(FBB));
  return Count;
}

MachineBasicBlock *
PIC16F1InstrInfo::getBranchDestBlock(const MachineInstr &MI) const {
  if (isCompareBranch(MI.getOpcode()))
    return MI.getOperand(3).getMBB();
  return MI.getOperand(0).getMBB();
}

bool PIC16F1InstrInfo::isBranchOffsetInRange(unsigned BranchOpc,
                                             int64_t BrOffset) const {
  if (BranchOpc == PIC16F1::JMPFAR)
    return true;
  // BRA reaches -256..+255 words from the word after it. The offset given
  // here is from the start of the pseudo, whose BRA is its last word, so
  // allow for the longest compare sequence.
  return BrOffset >= -480 && BrOffset <= 480;
}

void PIC16F1InstrInfo::insertIndirectBranch(MachineBasicBlock &MBB,
                                            MachineBasicBlock &NewDestBB,
                                            MachineBasicBlock &RestoreBB,
                                            const DebugLoc &DL, int64_t BrOffset,
                                            RegScavenger *RS) const {
  BuildMI(&MBB, DL, get(PIC16F1::JMPFAR)).addMBB(&NewDestBB);
}

void PIC16F1InstrInfo::buildCompareBranch(
    const MachineInstr &MI, SmallVectorImpl<PIC16F1RawInst> &Seq) const {
  using namespace PIC16F1;
  unsigned Opc = MI.getOpcode();
  unsigned CC = MI.getOperand(0).getImm();
  bool Is16 = Opc == BRCC16rr || Opc == BRCC16ri;
  bool IsImm = Opc == BRCC8ri || Opc == BRCC16ri;
  unsigned A = fileAddr(MI.getOperand(1).getReg());
  unsigned B = IsImm ? 0 : fileAddr(MI.getOperand(2).getReg());
  uint64_t K = IsImm ? uint64_t(MI.getOperand(2).getImm()) & 0xFFFF : 0;
  unsigned KLo = K & 0xFF;
  unsigned KHi = Is16 ? (K >> 8) & 0xFF : 0;

  const unsigned ToW = 0;
  auto F = [&](unsigned O, unsigned File, unsigned D) {
    Seq.push_back({O, File, D, 2, false});
  };
  auto L = [&](unsigned O, unsigned Lit) {
    Seq.push_back({O, Lit & 0xFF, 0, 1, false});
  };
  auto Branch = [&] { Seq.push_back({BRA, 0, 0, 1, true}); };
  // Branch if the STATUS bit has the given value.
  auto BranchIfStatus = [&](unsigned Bit, bool Set) {
    F(Set ? BTFSC : BTFSS, STATUS_ADDR, Bit);
    Branch();
  };

  switch (CC) {
  case PIC16F1CC::COND_EQ:
  case PIC16F1CC::COND_NE: {
    bool WantZ = CC == PIC16F1CC::COND_EQ;
    if (!Is16) {
      if (IsImm) {
        F(MOVF, A, ToW);
        if (KLo)
          L(XORLW, KLo);
      } else {
        F(MOVF, B, ToW);
        F(XORWF, A, ToW);
      }
    } else if (IsImm && K == 0) {
      F(MOVF, A, ToW);
      F(IORWF, A + 1, ToW);
    } else {
      F(MOVF, A, ToW);
      if (IsImm) {
        if (KLo)
          L(XORLW, KLo);
      } else {
        F(XORWF, B, ToW);
      }
      Seq.push_back({MOVWF, TMP0_ADDR, 0, 1, false});
      F(MOVF, A + 1, ToW);
      if (IsImm) {
        if (KHi)
          L(XORLW, KHi);
      } else {
        F(XORWF, B + 1, ToW);
      }
      F(IORWF, TMP0_ADDR, ToW);
    }
    BranchIfStatus(STATUS_Z, WantZ);
    return;
  }
  case PIC16F1CC::COND_ULT:
  case PIC16F1CC::COND_UGE: {
    // a - b leaves C set when there was no borrow, that is when a >= b.
    if (IsImm)
      L(MOVLW, KLo);
    else
      F(MOVF, B, ToW);
    F(SUBWF, A, ToW);
    if (Is16) {
      if (IsImm)
        L(MOVLW, KHi);
      else
        F(MOVF, B + 1, ToW);
      F(SUBWFB, A + 1, ToW);
    }
    BranchIfStatus(STATUS_C, CC == PIC16F1CC::COND_UGE);
    return;
  }
  case PIC16F1CC::COND_LT:
  case PIC16F1CC::COND_GE: {
    bool WantGE = CC == PIC16F1CC::COND_GE;
    unsigned Top = Is16 ? A + 1 : A;
    // Against zero the answer is the sign bit.
    if (IsImm && K == 0) {
      F(WantGE ? BTFSS : BTFSC, Top, 7);
      Branch();
      return;
    }
    // Flipping both sign bits turns a signed comparison into an unsigned one.
    if (!Is16 && IsImm) {
      unsigned KP = KLo ^ 0x80;
      if (KP == 0) {
        // Comparing with the most negative value: always greater or equal.
        if (WantGE)
          Branch();
        return;
      }
      F(MOVF, A, ToW);
      L(XORLW, 0x80);
      L(ADDLW, 0x100 - KP); // carry out when (a ^ 0x80) >= (k ^ 0x80)
      BranchIfStatus(STATUS_C, WantGE);
      return;
    }
    if (Is16) {
      if (IsImm)
        L(MOVLW, KLo);
      else
        F(MOVF, B, ToW);
      F(SUBWF, A, ToW);
    }
    F(MOVF, Top, ToW);
    L(XORLW, 0x80);
    Seq.push_back({MOVWF, TMP0_ADDR, 0, 1, false});
    if (IsImm) {
      L(MOVLW, KHi ^ 0x80);
    } else {
      F(MOVF, Is16 ? B + 1 : B, ToW);
      L(XORLW, 0x80);
    }
    F(Is16 ? SUBWFB : SUBWF, TMP0_ADDR, ToW);
    BranchIfStatus(STATUS_C, WantGE);
    return;
  }
  default:
    llvm_unreachable("invalid condition code");
  }
}

unsigned PIC16F1InstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  unsigned Opc = MI.getOpcode();
  if (isCompareBranch(Opc)) {
    SmallVector<PIC16F1RawInst, 12> Seq;
    buildCompareBranch(MI, Seq);
    return 2 * Seq.size();
  }
  switch (Opc) {
  case PIC16F1::JMP:
    return 2;
  case PIC16F1::JMPFAR:
    return 4;
  case PIC16F1::JMPreg:
    return 8;
  case TargetOpcode::INLINEASM:
  case TargetOpcode::INLINEASM_BR: {
    const MachineFunction *MF = MI.getParent()->getParent();
    return getInlineAsmLength(MI.getOperand(0).getSymbolName(),
                              MF->getTarget().getMCAsmInfo());
  }
  default:
    return MI.getDesc().getSize();
  }
}
