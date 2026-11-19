//===-- PIC16F1FrameLowering.cpp - Frame lowering ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//
// The data stack is addressed through FSR1 and grows down. FSR1 points at
// the lowest byte in use. Return addresses are on the hardware stack, so a
// frame holds only saved registers, locals, spills and outgoing arguments.
//
// An interrupt handler runs on its own stack: it loads FSR1 with
// __isr_stack_top on entry, and RETFIE puts the interrupted value back from
// the shadow register. Because of that, code may move FSR1 temporarily to
// reach a distant slot without an interrupt overwriting live data.
//
//===----------------------------------------------------------------------===//

#include "PIC16F1FrameLowering.h"
#include "PIC16F1InstrInfo.h"
#include "PIC16F1Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "PIC16F1MachineFunctionInfo.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

PIC16F1FrameLowering::PIC16F1FrameLowering(const PIC16F1Subtarget &STI)
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, Align(1), 0,
                          Align(1)),
      STI(STI) {}

void PIC16F1FrameLowering::adjustSP(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator I,
                                    const DebugLoc &DL, int64_t Amount) const {
  const PIC16F1InstrInfo &TII = *STI.getInstrInfo();
  if (Amount == 0)
    return;
  // ADDFSR takes -32..31. Up to four of them are shorter than or equal to
  // the arithmetic form, and they leave W and the flags alone.
  if (Amount >= -128 && Amount <= 124) {
    while (Amount != 0) {
      int64_t Step = Amount > 0 ? std::min<int64_t>(Amount, 31)
                                : std::max<int64_t>(Amount, -32);
      BuildMI(MBB, I, DL, TII.get(PIC16F1::ADDFSR)).addImm(1).addImm(Step);
      Amount -= Step;
    }
    return;
  }
  BuildMI(MBB, I, DL, TII.get(PIC16F1::MOVLW)).addImm(Amount & 0xFF);
  BuildMI(MBB, I, DL, TII.get(PIC16F1::ADDWF))
      .addImm(PIC16F1::FSR1L_ADDR)
      .addImm(1);
  BuildMI(MBB, I, DL, TII.get(PIC16F1::MOVLW)).addImm((Amount >> 8) & 0xFF);
  BuildMI(MBB, I, DL, TII.get(PIC16F1::ADDWFC))
      .addImm(PIC16F1::FSR1H_ADDR)
      .addImm(1);
}

void PIC16F1FrameLowering::emitPrologue(MachineFunction &MF,
                                        MachineBasicBlock &MBB) const {
  const PIC16F1InstrInfo &TII = *STI.getInstrInfo();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineBasicBlock::iterator I = MBB.begin();
  DebugLoc DL;

  // An interrupt handler runs on a stack of its own, unless it uses none:
  // it then leaves FSR1 alone.
  bool UsesStack = MFI.getStackSize() != 0 || MFI.hasCalls() ||
                   MFI.hasVarSizedObjects() || MFI.getNumObjects() != 0;
  if (MF.getFunction().hasFnAttribute("interrupt") && UsesStack) {
    const char *Top = "__isr_stack_top";
    BuildMI(MBB, I, DL, TII.get(PIC16F1::MOVLW))
        .addExternalSymbol(Top, PIC16F1II::MO_LO8);
    BuildMI(MBB, I, DL, TII.get(PIC16F1::MOVWF)).addImm(PIC16F1::FSR1L_ADDR);
    BuildMI(MBB, I, DL, TII.get(PIC16F1::MOVLW))
        .addExternalSymbol(Top, PIC16F1II::MO_HI8);
    BuildMI(MBB, I, DL, TII.get(PIC16F1::MOVWF)).addImm(PIC16F1::FSR1H_ADDR);
  }
  // The pushes of the callee-saved registers come next and have moved the
  // stack pointer over their part of the frame already.
  while (I != MBB.end() && I->getFlag(MachineInstr::FrameSetup))
    ++I;
  adjustSP(MBB, I, DL,
           -int64_t(MFI.getStackSize() - MFI.getCalleeSavedInfo().size()));
}

void PIC16F1FrameLowering::emitEpilogue(MachineFunction &MF,
                                        MachineBasicBlock &MBB) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  // RETFIE restores FSR1, so an interrupt handler that pops nothing has
  // nothing to undo.
  if (MF.getFunction().hasFnAttribute("interrupt") &&
      MFI.getCalleeSavedInfo().empty())
    return;
  MachineBasicBlock::iterator I = MBB.getFirstTerminator();
  DebugLoc DL = I != MBB.end() ? I->getDebugLoc() : DebugLoc();
  // Back over the pops of the callee-saved registers: the stack pointer
  // must be at the first of them when they start.
  while (I != MBB.begin() &&
         std::prev(I)->getFlag(MachineInstr::FrameDestroy))
    --I;
  adjustSP(MBB, I, DL, MFI.getStackSize() - MFI.getCalleeSavedInfo().size());
}

MachineBasicBlock::iterator PIC16F1FrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  // The outgoing-argument area is part of the frame.
  return MBB.erase(I);
}

void PIC16F1FrameLowering::determineCalleeSaves(MachineFunction &MF,
                                                BitVector &SavedRegs,
                                                RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);
  // An interrupt handler saves TMP0 if anything may change it. Every
  // compare-and-branch is declared to, but most do not use it.
  if (!SavedRegs.test(PIC16F1::TMP0))
    return;
  const PIC16F1InstrInfo &TII = *STI.getInstrInfo();
  const TargetRegisterInfo *TRI = STI.getRegisterInfo();
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      if (!MI.modifiesRegister(PIC16F1::TMP0, TRI))
        continue;
      if (!PIC16F1InstrInfo::isCompareBranch(MI.getOpcode()))
        return;
      SmallVector<PIC16F1RawInst, 12> Seq;
      TII.buildCompareBranch(MI, Seq);
      for (const PIC16F1RawInst &R : Seq)
        if (!R.IsBranch && R.Op0 == PIC16F1::TMP0_ADDR)
          return;
    }
  }
  SavedRegs.reset(PIC16F1::TMP0);
}

static cl::opt<unsigned> StaticSlotLimit(
    "pic16f1-static-slot-limit", cl::Hidden, cl::init(64),
    cl::desc("In a function with a static frame, the largest frame object, "
             "in bytes, that gets a fixed address; 0 leaves all of them on "
             "the stack"));

unsigned llvm::pic16f1StaticSlotLimit() { return StaticSlotLimit; }

/// The instructions whose frame-index operand can become a fixed address.
static bool hasDirectForm(unsigned Opc) {
  switch (Opc) {
  case PIC16F1::LD8fi:
  case PIC16F1::LD16fi:
  case PIC16F1::ST8fi:
  case PIC16F1::ST16fi:
  case PIC16F1::STI8fi:
  case PIC16F1::STI16fi:
  case PIC16F1::LEAfi:
    return true;
  default:
    return false;
  }
}

/// In a function that cannot be re-entered (PIC16F1StaticFrames.cpp marks
/// them) and that uses FSR1 as a register, the spill slots and the variables
/// still in the frame get fixed addresses too: they are packed into blocks of up to 16 bytes, which the
/// linker keeps within a bank, and leave the stack frame. What stays on the
/// stack is the saved registers and the outgoing arguments.
void PIC16F1FrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *RS) const {
  const Function &F = MF.getFunction();
  if (!StaticSlotLimit || !F.hasFnAttribute("pic16f1-static-frame"))
    return;
  MachineFrameInfo &MFI = MF.getFrameInfo();
  auto *FuncInfo = MF.getInfo<PIC16F1MachineFunctionInfo>();
  // A slot on the stack is reached without selecting a bank, so the slots
  // stay there unless FSR1 is busy with something else: that is, unless the
  // register allocator has put a value other than the stack pointer in it.
  if (!FuncInfo->fsr1IsRegister())
    return;
  const TargetRegisterInfo *TRI = STI.getRegisterInfo();
  bool FSR1Used = false;
  for (const MachineBasicBlock &MBB : MF)
    for (const MachineInstr &MI : MBB)
      if (!MI.getFlag(MachineInstr::FrameSetup) &&
          !MI.getFlag(MachineInstr::FrameDestroy) &&
          MI.modifiesRegister(PIC16F1::SP, TRI))
        FSR1Used = true;
  if (!FSR1Used)
    return;

  BitVector Stays(MFI.getObjectIndexEnd());
  for (const CalleeSavedInfo &I : MFI.getCalleeSavedInfo())
    if (I.getFrameIdx() >= 0 && I.getFrameIdx() < MFI.getObjectIndexEnd())
      Stays.set(I.getFrameIdx());
  for (const MachineBasicBlock &MBB : MF)
    for (const MachineInstr &MI : MBB)
      if (!hasDirectForm(MI.getOpcode()) && !MI.isDebugInstr())
        for (const MachineOperand &MO : MI.operands())
          if (MO.isFI() && MO.getIndex() >= 0)
            Stays.set(MO.getIndex());

  struct Block {
    uint64_t Size = 0;
    SmallVector<std::pair<int, uint64_t>, 8> Members;
  };
  SmallVector<Block, 4> Blocks;
  const uint64_t BlockLimit = 16;
  for (int FI = 0, E = MFI.getObjectIndexEnd(); FI != E; ++FI) {
    if (Stays.test(FI) || MFI.isDeadObjectIndex(FI) ||
        MFI.isVariableSizedObjectIndex(FI))
      continue;
    uint64_t Size = MFI.getObjectSize(FI);
    if (Size == 0 || Size > StaticSlotLimit)
      continue;
    Block *Into = nullptr;
    if (Size <= BlockLimit)
      for (Block &B : Blocks)
        if (B.Size + Size <= BlockLimit) {
          Into = &B;
          break;
        }
    if (!Into)
      Into = &Blocks.emplace_back();
    Into->Members.push_back({FI, Into->Size});
    Into->Size += Size;
  }
  // With FSR1 in use as a register, nothing can be reached through it.
  for (int FI = 0, E = MFI.getObjectIndexEnd(); FI != E; ++FI)
      if (!MFI.isDeadObjectIndex(FI) && !FuncInfo->getStaticObject(FI)) {
        bool Pending = false;
        for (Block &B : Blocks)
          for (auto &[Member, At] : B.Members)
            Pending |= Member == FI;
        bool Saved = false;
        for (const CalleeSavedInfo &I : MFI.getCalleeSavedInfo())
          Saved |= I.getFrameIdx() == FI;
        if (!Pending && !Saved)
          report_fatal_error("PIC16F1: a frame object needs the stack in a "
                             "function that uses FSR1 as a register");
      }
  if (Blocks.empty())
    return;

  // The variables belong to the module, like the ones the IR pass makes.
  Module &M = const_cast<Module &>(*F.getParent());
  Type *I8 = Type::getInt8Ty(M.getContext());
  for (Block &B : Blocks) {
    Type *Ty = ArrayType::get(I8, B.Size);
    auto *GV = new GlobalVariable(M, Ty, /*isConstant=*/false,
                                  GlobalValue::InternalLinkage,
                                  Constant::getNullValue(Ty),
                                  F.getName() + ".slots");
    GV->setAlignment(Align(1));
    GV->setSection((".noinit." + GV->getName()).str());
    for (auto &[FI, At] : B.Members) {
      FuncInfo->setStaticObject(FI, GV, At);
      MFI.RemoveStackObject(FI);
    }
  }
}

// MOVWI --FSR1 and MOVIW FSR1++ (see printIndMode for the encoding).
static const unsigned PushMode = 5, PopMode = 6;

bool PIC16F1FrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return false;
  const PIC16F1InstrInfo &TII = *STI.getInstrInfo();
  DebugLoc DL;
  // The first register pushed lands just below the stack pointer at entry,
  // which is where the first spill slot is: the generic code lays the
  // slots out downwards in this same order.
  for (const CalleeSavedInfo &I : CSI) {
    MCRegister Reg = I.getReg();
    if (!MBB.isLiveIn(Reg))
      MBB.addLiveIn(Reg);
    BuildMI(MBB, MI, DL, TII.get(PIC16F1::MOVF))
        .addImm(TII.fileAddr(Reg))
        .addImm(0)
        .setMIFlag(MachineInstr::FrameSetup);
    BuildMI(MBB, MI, DL, TII.get(PIC16F1::MOVWIm))
        .addImm(PushMode)
        .setMIFlag(MachineInstr::FrameSetup);
  }
  return true;
}

bool PIC16F1FrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return false;
  const PIC16F1InstrInfo &TII = *STI.getInstrInfo();
  DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();
  for (const CalleeSavedInfo &I : llvm::reverse(CSI)) {
    BuildMI(MBB, MI, DL, TII.get(PIC16F1::MOVIWm))
        .addImm(PopMode)
        .setMIFlag(MachineInstr::FrameDestroy);
    BuildMI(MBB, MI, DL, TII.get(PIC16F1::MOVWF))
        .addImm(TII.fileAddr(I.getReg()))
        .setMIFlag(MachineInstr::FrameDestroy);
  }
  return true;
}
