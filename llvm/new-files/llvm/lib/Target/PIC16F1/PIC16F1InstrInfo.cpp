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
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/Module.h"
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
    } else if (IsImm && (KHi == 0 || KLo == 0)) {
      // One byte must equal a constant and the other must be zero.
      F(MOVF, KHi == 0 ? A : A + 1, ToW);
      L(XORLW, KHi == 0 ? KLo : KHi);
      F(IORWF, KHi == 0 ? A + 1 : A, ToW);
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
    if (Is16 && IsImm && KLo == 0) {
      // The low byte cannot borrow: only the high bytes decide.
      L(MOVLW, KHi);
      F(SUBWF, A + 1, ToW);
      BranchIfStatus(STATUS_C, CC == PIC16F1CC::COND_UGE);
      return;
    }
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
    // With a constant whose low byte is zero, only the high bytes decide.
    if (IsImm && (!Is16 || KLo == 0)) {
      unsigned KP = (Is16 ? KHi : KLo) ^ 0x80;
      if (KP == 0) {
        // Comparing with the most negative value: always greater or equal.
        if (WantGE)
          Branch();
        return;
      }
      F(MOVF, Top, ToW);
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
  case PIC16F1CC::COND_BC:
  case PIC16F1CC::COND_BS:
    assert(Opc == BRCC8ri && "bit test needs the 8-bit immediate form");
    // BTFSC skips the branch when the bit is clear.
    F(CC == PIC16F1CC::COND_BS ? BTFSC : BTFSS, A, K & 7);
    Branch();
    return;
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
    // MOVLP and GOTO; in the one-page code model only the GOTO.
    return MI.getMF()->getTarget().getCodeModel() == CodeModel::Small ? 2 : 4;
  case PIC16F1::JMPreg:
    return 8;
  case PIC16F1::BRTABLE:
  case PIC16F1::BRTABLEW: {
    // The load of W, BRW, and an entry of one or two words for each case.
    const MachineFunction *MF = MI.getMF();
    const MachineJumpTableInfo *Tables = MF->getJumpTableInfo();
    unsigned Index = MI.getOperand(Opc == PIC16F1::BRTABLE ? 1 : 0).getIndex();
    unsigned Entries = Tables ? Tables->getJumpTables()[Index].MBBs.size() : 0;
    bool OnePage = MF->getTarget().getCodeModel() == CodeModel::Small;
    return 2 * ((Opc == PIC16F1::BRTABLE ? 2 : 1) + Entries * (OnePage ? 1 : 2));
  }
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

//===----------------------------------------------------------------------===//
// Machine outliner
//===----------------------------------------------------------------------===//

namespace {
// Default: the sequence, then a return. TailCall: the sequence ends in a
// return, and is jumped to. Thunk: the sequence ends in a call, which
// becomes a jump, so that the callee returns for the outlined function.
enum MachineOutlinerClass {
  MachineOutlinerDefault,
  MachineOutlinerTailCall,
  MachineOutlinerThunk
};
} // namespace

bool PIC16F1InstrInfo::inOneBank(const GlobalValue *GV, int64_t Off) {
  const auto *Var = dyn_cast<GlobalVariable>(GV);
  // A section the program names may hold other variables too; the blocks
  // PIC16F1StaticFrames makes each have their own.
  if (!Var || !Var->getValueType()->isSized() ||
      (Var->hasSection() && !Var->getSection().starts_with(".noinit.")))
    return false;
  uint64_t Size =
      Var->getParent()->getDataLayout().getTypeAllocSize(Var->getValueType());
  return Size <= 16 && Off >= 0 && uint64_t(Off) < Size;
}

bool PIC16F1InstrInfo::isSkip(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case PIC16F1::BTFSC:
  case PIC16F1::BTFSS:
  case PIC16F1::DECFSZ:
  case PIC16F1::INCFSZ:
    return true;
  default:
    return false;
  }
}

bool PIC16F1InstrInfo::shouldOutlineFromFunctionByDefault(
    MachineFunction &MF) const {
  // Each use of an outlined sequence costs the four cycles of a call and
  // return, so only where size was asked for.
  return MF.getFunction().hasOptSize();
}

bool PIC16F1InstrInfo::isFunctionSafeToOutlineFrom(
    MachineFunction &MF, bool OutlineFromLinkOnceODRs) const {
  const Function &F = MF.getFunction();
  if (!OutlineFromLinkOnceODRs && F.hasLinkOnceODRLinkage())
    return false;
  // setjmp keeps the return address of its call to come back to. Called
  // from an outlined sequence, that would be an address inside the
  // sequence, whose own return address is gone by the time longjmp runs.
  if (F.callsFunctionThatReturnsTwice())
    return false;
  // Of a function compiled several ways (PIC16F1StaticFrames.cpp) only the
  // static version is outlined from. They are all nearly the same code, and
  // counting both would make sequences look repeated that the program has
  // once; the static version is the one usually kept.
  if (F.hasFnAttribute("pic16f1-dual") &&
      (!F.hasFnAttribute("pic16f1-dual-of") ||
       F.hasFnAttribute("pic16f1-isr-copy")))
    return false;
  return true;
}

/// The instruction that runs before \p MI when control arrives from above,
/// or null at the start of the function.
static const MachineInstr *instrBefore(const MachineInstr &MI) {
  const MachineBasicBlock *MBB = MI.getParent();
  for (auto It = std::next(MI.getReverseIterator()); It != MBB->rend(); ++It)
    if (!It->isMetaInstruction())
      return &*It;
  MachineFunction::const_iterator B = MBB->getIterator();
  while (B != MBB->getParent()->begin()) {
    --B;
    for (const MachineInstr &Prev : llvm::reverse(*B))
      if (!Prev.isMetaInstruction())
        return &Prev;
  }
  return nullptr;
}

/// The file address \p MI names (without the bank), or -1 if it names none
/// or names a symbol.
static int fileOperand(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case PIC16F1::ADDWF:
  case PIC16F1::ADDWFC:
  case PIC16F1::ANDWF:
  case PIC16F1::ASRF:
  case PIC16F1::LSLF:
  case PIC16F1::LSRF:
  case PIC16F1::COMF:
  case PIC16F1::DECF:
  case PIC16F1::INCF:
  case PIC16F1::IORWF:
  case PIC16F1::MOVF:
  case PIC16F1::RLF:
  case PIC16F1::RRF:
  case PIC16F1::SUBWF:
  case PIC16F1::SUBWFB:
  case PIC16F1::SWAPF:
  case PIC16F1::XORWF:
  case PIC16F1::DECFSZ:
  case PIC16F1::INCFSZ:
  case PIC16F1::CLRF:
  case PIC16F1::MOVWF:
  case PIC16F1::BCF:
  case PIC16F1::BSF:
  case PIC16F1::BTFSC:
  case PIC16F1::BTFSS:
    return MI.getOperand(0).isImm() ? int(MI.getOperand(0).getImm() & 0x7F)
                                    : -1;
  default:
    return -1;
  }
}

static bool writesFile(const MachineInstr &MI, unsigned File) {
  if (fileOperand(MI) != int(File))
    return false;
  switch (MI.getOpcode()) {
  case PIC16F1::MOVWF:
  case PIC16F1::CLRF:
  case PIC16F1::BCF:
  case PIC16F1::BSF:
    return true;
  case PIC16F1::BTFSC:
  case PIC16F1::BTFSS:
    return false;
  default:
    // The destination bit is the second operand.
    return MI.getOperand(1).getImm() == 1;
  }
}

std::optional<std::unique_ptr<outliner::OutlinedFunction>>
PIC16F1InstrInfo::getOutliningCandidateInfo(
    const MachineModuleInfo &MMI,
    std::vector<outliner::Candidate> &RepeatedSequenceLocs,
    unsigned MinRepeats) const {
  outliner::Candidate &First = RepeatedSequenceLocs[0];
  bool Small =
      First.getMF()->getTarget().getCodeModel() == CodeModel::Small;

  unsigned SequenceSize = 0;
  for (const MachineInstr &MI : First)
    SequenceSize += getInstSizeInBytes(MI);

  // A sequence that ends in a skip would skip the return that follows it
  // in the outlined function.
  const MachineInstr &Last = First.back();
  if (isSkip(Last))
    return std::nullopt;

  // The call to the outlined function sets PCLATH, except in the one-page
  // code model. A call or computed jump in the sequence must then have its
  // own setting of PCLATH inside the sequence.
  if (!Small) {
    bool PageSet = false;
    for (const MachineInstr &MI : First) {
      if (MI.getOpcode() == PIC16F1::MOVLP ||
          writesFile(MI, PIC16F1::PCLATH_ADDR)) {
        PageSet = true;
      } else if (MI.isCall() || MI.getOpcode() == PIC16F1::GOTO ||
                 writesFile(MI, PIC16F1::PCL_ADDR)) {
        if (!PageSet)
          return std::nullopt;
        PageSet = false;
      }
    }
  }

  // A sequence that follows a skip runs only in part when the skip is
  // taken; a call in its place would be skipped whole.
  llvm::erase_if(RepeatedSequenceLocs, [](outliner::Candidate &C) {
    const MachineInstr *Prev = instrBefore(C.front());
    return Prev && (isSkip(*Prev) || Prev->isInlineAsm());
  });
  if (RepeatedSequenceLocs.size() < MinRepeats)
    return std::nullopt;

  unsigned CallSize = Small ? 2 : 4;
  if (Last.isReturn()) {
    for (outliner::Candidate &C : RepeatedSequenceLocs)
      C.setCallInfo(MachineOutlinerTailCall, CallSize);
    return std::make_unique<outliner::OutlinedFunction>(
        RepeatedSequenceLocs, SequenceSize, 0, MachineOutlinerTailCall);
  }
  // A call at the end that no skip guards can be a jump.
  bool EndsInCall = Last.getOpcode() == PIC16F1::CALL;
  if (EndsInCall) {
    const MachineInstr *BeforeLast = nullptr;
    for (const MachineInstr &MI : First) {
      if (&MI == &Last)
        break;
      if (!MI.isMetaInstruction())
        BeforeLast = &MI;
    }
    EndsInCall = !BeforeLast || !isSkip(*BeforeLast);
  }
  for (outliner::Candidate &C : RepeatedSequenceLocs)
    C.setCallInfo(MachineOutlinerDefault, CallSize);
  return std::make_unique<outliner::OutlinedFunction>(
      RepeatedSequenceLocs, SequenceSize, EndsInCall ? 0 : 2,
      EndsInCall ? MachineOutlinerThunk : MachineOutlinerDefault);
}

outliner::InstrType
PIC16F1InstrInfo::getOutliningTypeImpl(const MachineModuleInfo &MMI,
                                       MachineBasicBlock::iterator &MIT,
                                       unsigned Flags) const {
  MachineInstr &MI = *MIT;
  if (MI.isCFIInstruction())
    return outliner::InstrType::Illegal;
  switch (MI.getOpcode()) {
  // The generic code lets through only a terminator that ends the function.
  // A return from an interrupt would not be reached by a jump from a
  // sequence that began as ordinary code, but nothing forbids sharing it.
  case PIC16F1::RETURN:
  case PIC16F1::RETFIE:
    return outliner::InstrType::Legal;
  // Depends on where it is.
  case PIC16F1::BRW:
  case PIC16F1::BRA:
  case PIC16F1::GOTO:
    return outliner::InstrType::Illegal;
  default:
    break;
  }
  if (MI.isTerminator() || MI.isPseudo())
    return outliner::InstrType::Illegal;
  // Reading PCL gives the address of the instruction; writing it jumps.
  if (fileOperand(MI) == PIC16F1::PCL_ADDR)
    return outliner::InstrType::Illegal;
  return outliner::InstrType::Legal;
}

void PIC16F1InstrInfo::buildOutlinedFrame(
    MachineBasicBlock &MBB, MachineFunction &MF,
    const outliner::OutlinedFunction &OF) const {
  if (OF.FrameConstructionID == MachineOutlinerTailCall)
    return;
  if (OF.FrameConstructionID == MachineOutlinerThunk) {
    MachineInstr &Call = MBB.back();
    assert(Call.getOpcode() == PIC16F1::CALL && "thunk must end in a call");
    MachineInstrBuilder Jump = BuildMI(MF, DebugLoc(), get(PIC16F1::GOTO));
    for (const MachineOperand &MO : Call.operands())
      if (!MO.isReg() || MO.isUse())
        Jump.add(MO);
    MBB.insert(MBB.end(), Jump);
    Call.eraseFromParent();
    return;
  }
  MBB.insert(MBB.end(), BuildMI(MF, DebugLoc(), get(PIC16F1::RETURN)));
}

MachineBasicBlock::iterator PIC16F1InstrInfo::insertOutlinedCall(
    Module &M, MachineBasicBlock &MBB, MachineBasicBlock::iterator &It,
    MachineFunction &MF, outliner::Candidate &C) const {
  const GlobalValue *Callee = M.getNamedValue(MF.getName());
  bool Small = MF.getTarget().getCodeModel() == CodeModel::Small;
  unsigned Opc = C.CallConstructionID == MachineOutlinerTailCall
                     ? PIC16F1::GOTO
                     : PIC16F1::CALL;
  It = MBB.insert(It, BuildMI(MF, DebugLoc(), get(Opc)).addGlobalAddress(Callee));
  if (!Small)
    MBB.insert(It, BuildMI(MF, DebugLoc(), get(PIC16F1::MOVLP))
                       .addGlobalAddress(Callee, 0, PIC16F1II::MO_PAGE));
  return It;
}
