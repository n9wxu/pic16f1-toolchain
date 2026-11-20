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
#include "MCTargetDesc/PIC16F1Dwarf.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
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

const GlobalVariable *llvm::pic16f1StackPointerCell(const Module &M) {
  Module &Mod = const_cast<Module &>(M);
  Type *Ty = ArrayType::get(Type::getInt8Ty(Mod.getContext()), 2);
  return cast<GlobalVariable>(Mod.getOrInsertGlobal("__sp", Ty));
}

static bool hasStaticFrame(const MachineFunction &MF) {
  return MF.getFunction().hasFnAttribute("pic16f1-static-frame");
}

bool llvm::pic16f1UsesStackPointer(const MachineFunction &MF) {
  if (hasStaticFrame(MF))
    return false;
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  // An interrupt handler runs on a stack of its own, unless it uses none.
  if (MF.getFunction().hasFnAttribute("interrupt"))
    return MFI.getStackSize() != 0 || MFI.hasCalls() ||
           MFI.hasVarSizedObjects() || MFI.getNumObjects() != 0;
  // Its own frame, or arguments that arrived on the stack.
  return MFI.getStackSize() != 0 || MFI.getNumFixedObjects() != 0 ||
         MFI.hasVarSizedObjects();
}

bool llvm::pic16f1HasFrameBase(const MachineFunction &MF) {
  return !hasStaticFrame(MF) && MF.getFrameInfo().hasVarSizedObjects() &&
         !MF.getFunction().hasFnAttribute("interrupt");
}

/// The variable an interrupt handler keeps the interrupted code's stack
/// pointer in while its own callees use the handler's stack.
static const GlobalVariable *savedStackPointer(MachineFunction &MF) {
  const Function &F = MF.getFunction();
  Module &M = const_cast<Module &>(*F.getParent());
  // (One handler to a program, and one place for this: the linker scripts
  // put it straight after the cell, where a debugger looks for it.)
  std::string Name = "__sp_saved";
  if (GlobalVariable *GV = M.getNamedGlobal(Name))
    return GV;
  Type *Ty = ArrayType::get(Type::getInt8Ty(M.getContext()), 2);
  auto *GV = new GlobalVariable(M, Ty, /*isConstant=*/false,
                                GlobalValue::InternalLinkage,
                                Constant::getNullValue(Ty), Name);
  GV->setAlignment(Align(1));
  GV->setSection(".noinit.__sp_saved");
  return GV;
}

void PIC16F1FrameLowering::emitPrologue(MachineFunction &MF,
                                        MachineBasicBlock &MBB) const {
  const PIC16F1InstrInfo &TII = *STI.getInstrInfo();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineBasicBlock::iterator I = MBB.begin();
  DebugLoc DL;

  // A static frame needs nothing set up, and no stack pointer.
  if (!pic16f1UsesStackPointer(MF))
    return;
  bool IsInterrupt = MF.getFunction().hasFnAttribute("interrupt");
  if (IsInterrupt) {
    const char *Top = "__isr_stack_top";
    BuildMI(MBB, I, DL, TII.get(PIC16F1::MOVLW))
        .addExternalSymbol(Top, PIC16F1II::MO_LO8);
    BuildMI(MBB, I, DL, TII.get(PIC16F1::MOVWF)).addImm(PIC16F1::FSR1L_ADDR);
    BuildMI(MBB, I, DL, TII.get(PIC16F1::MOVLW))
        .addExternalSymbol(Top, PIC16F1II::MO_HI8);
    BuildMI(MBB, I, DL, TII.get(PIC16F1::MOVWF)).addImm(PIC16F1::FSR1H_ADDR);
  } else {
    // FSR1 may hold anything on entry; the stack pointer is in its cell.
    BuildMI(MBB, I, DL, TII.get(PIC16F1::SPLOAD));
  }
  // The pushes of the callee-saved registers come next and have moved the
  // stack pointer over their part of the frame already.
  while (I != MBB.end() && I->getFlag(MachineInstr::FrameSetup))
    ++I;
  adjustSP(MBB, I, DL,
           -int64_t(MFI.getStackSize() - MFI.getCalleeSavedInfo().size()));
  // With variable-sized objects the cell moves down under them while FSR1
  // stays here; RP6 remembers where that is across calls.
  bool HasBase = pic16f1HasFrameBase(MF);
  if (HasBase) {
    for (unsigned B = 0; B != 2; ++B) {
      BuildMI(MBB, I, DL, TII.get(PIC16F1::MOVF))
          .addImm(PIC16F1::FSR1L_ADDR + B)
          .addImm(0);
      BuildMI(MBB, I, DL, TII.get(PIC16F1::MOVWF))
          .addImm(TII.fileAddr(B ? PIC16F1::R13 : PIC16F1::R12));
    }
  }
  // For a debugger: the caller's stack level (see
  // MCTargetDesc/PIC16F1Dwarf.h). Until here the rule that holds for every
  // function is right: the cell is still the caller's level. While the
  // cell is being written, two bytes in four instructions, it is neither
  // level, and the rule goes by FSR1 itself, which the innermost frame can;
  // from then on by the cell, or by RP6 where that is the frame's base.
  // The epilogue goes back to FSR1 once that is the caller's level again.
  const bool Describe = MF.needsFrameMoves() && !IsInterrupt;
  // An interrupt handler that makes calls gives them its own stack and
  // keeps the interrupted code's level beside the cell meanwhile.
  if (MF.needsFrameMoves() && IsInterrupt && MFI.hasCalls()) {
    SmallString<48> Saved, Rule, Level, Common;
    PIC16F1Dwarf::savedCell(Saved);
    PIC16F1Dwarf::valExpression(Rule, PIC16F1Dwarf::FSR1, Saved);
    PIC16F1Dwarf::stackLevel(Level);
    PIC16F1Dwarf::valExpression(Common, PIC16F1Dwarf::FSR1, Level);
    MF.getInfo<PIC16F1MachineFunctionInfo>()->setFrameRules(
        MF.addFrameInst(MCCFIInstruction::createEscape(nullptr, Rule.str())),
        MF.addFrameInst(MCCFIInstruction::createEscape(nullptr, Common.str())));
  }
  auto rule = [&](const SmallVectorImpl<char> &Level) {
    SmallString<48> Rule;
    PIC16F1Dwarf::valExpression(Rule, PIC16F1Dwarf::FSR1, Level);
    return MF.addFrameInst(MCCFIInstruction::createEscape(nullptr, Rule.str()));
  };
  auto byRegister = [&](unsigned Reg, uint64_t Plus) {
    SmallString<16> Level;
    PIC16F1Dwarf::op(Level, dwarf::DW_OP_breg0 + Reg);
    PIC16F1Dwarf::op(Level, 0);
    if (Plus) {
      PIC16F1Dwarf::op(Level, dwarf::DW_OP_plus_uconst);
      PIC16F1Dwarf::uleb(Level, Plus);
    }
    return rule(Level);
  };
  auto say = [&](unsigned Index) {
    BuildMI(MBB, I, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(Index)
        .setMIFlag(MachineInstr::FrameSetup);
  };
  const bool StoresCell = MFI.hasCalls() || HasBase;
  if (Describe && StoresCell)
    say(HasBase ? byRegister(PIC16F1Dwarf::RP6, MFI.getStackSize())
                : byRegister(PIC16F1Dwarf::FSR1, MFI.getStackSize()));
  // What this function calls starts below its frame.
  if (StoresCell) {
    if (IsInterrupt) {
      BuildMI(MBB, I, DL, TII.get(PIC16F1::SPSWITCH))
          .addGlobalAddress(savedStackPointer(MF));
      int Saved = MF.getInfo<PIC16F1MachineFunctionInfo>()->frameRule();
      if (Saved >= 0)
        BuildMI(MBB, I, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(Saved)
            .setMIFlag(MachineInstr::FrameSetup);
    } else
      BuildMI(MBB, I, DL, TII.get(PIC16F1::SPSTORE));
  }
  if (Describe && StoresCell) {
    unsigned Frame;
    if (HasBase) {
      Frame = byRegister(PIC16F1Dwarf::RP6, MFI.getStackSize());
    } else {
      SmallString<48> Level;
      PIC16F1Dwarf::stackLevel(Level);
      PIC16F1Dwarf::op(Level, dwarf::DW_OP_plus_uconst);
      PIC16F1Dwarf::uleb(Level, MFI.getStackSize());
      Frame = rule(Level);
    }
    say(Frame);
    MF.getInfo<PIC16F1MachineFunctionInfo>()->setFrameRules(
        Frame, byRegister(PIC16F1Dwarf::FSR1, 0));
  }
  // (A function with a stack frame and no calls leaves the cell alone, and
  // the rule for every function stays right.)
}

void PIC16F1FrameLowering::emitEpilogue(MachineFunction &MF,
                                        MachineBasicBlock &MBB) const {
  const PIC16F1InstrInfo &TII = *STI.getInstrInfo();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  if (!pic16f1UsesStackPointer(MF))
    return;
  bool IsInterrupt = MF.getFunction().hasFnAttribute("interrupt");
  MachineBasicBlock::iterator Term = MBB.getFirstTerminator();
  DebugLoc DL = Term != MBB.end() ? Term->getDebugLoc() : DebugLoc();
  // Back over the pops of the callee-saved registers: the stack pointer
  // must be at the first of them when they start.
  MachineBasicBlock::iterator I = Term;
  while (I != MBB.begin() &&
         std::prev(I)->getFlag(MachineInstr::FrameDestroy))
    --I;
  if (IsInterrupt) {
    if (MFI.hasCalls()) {
      BuildMI(MBB, I, DL, TII.get(PIC16F1::SPBACK))
          .addGlobalAddress(savedStackPointer(MF));
      // For a debugger: the cell is the interrupted code's level again.
      int Common = MF.getInfo<PIC16F1MachineFunctionInfo>()->commonRule();
      if (Common >= 0)
        BuildMI(MBB, I, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(Common)
            .setMIFlag(MachineInstr::FrameDestroy);
    }
    // RETFIE restores FSR1, so a handler that pops nothing has nothing
    // more to undo.
    if (MFI.getCalleeSavedInfo().empty())
      return;
  }
  adjustSP(MBB, I, DL, MFI.getStackSize() - MFI.getCalleeSavedInfo().size());
  // After the pops FSR1 is what it was on entry: back in the cell with it.
  // That also gives back every variable-sized object.
  if ((MFI.hasCalls() || pic16f1HasFrameBase(MF)) && !IsInterrupt) {
    // For a debugger: FSR1 is the caller's level from here to the return.
    // (The blocks that follow this one get the frame's rule back:
    // PIC16F1ExpandPseudo.)
    int Leaving = MF.getInfo<PIC16F1MachineFunctionInfo>()->commonRule();
    if (Leaving >= 0)
      BuildMI(MBB, Term, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(Leaving)
          .setMIFlag(MachineInstr::FrameDestroy);
    BuildMI(MBB, Term, DL, TII.get(PIC16F1::SPSTORE));
  }
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
  // The prologue puts the frame base in RP6.
  if (pic16f1HasFrameBase(MF)) {
    SavedRegs.set(PIC16F1::R12);
    SavedRegs.set(PIC16F1::R13);
  }
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
    cl::desc("The largest variable, in bytes, a function may have and still "
             "get a static frame. (Fixed variables of different functions "
             "do not share memory yet, so large ones stay on the stack.)"));

unsigned llvm::pic16f1StaticSlotLimit() { return StaticSlotLimit; }

StackOffset
PIC16F1FrameLowering::getFrameIndexReference(const MachineFunction &MF, int FI,
                                             Register &FrameReg) const {
  if (MF.getInfo<PIC16F1MachineFunctionInfo>()->getStaticObject(FI)) {
    FrameReg = PIC16F1::SP;
    return StackOffset::getFixed(0);
  }
  return TargetFrameLowering::getFrameIndexReference(MF, FI, FrameReg);
}

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

/// In a function with a static frame (PIC16F1StaticFrames.cpp), every frame
/// object gets a fixed address: the variables still in the frame, the spill
/// slots and the saved registers. They are packed into blocks of up to 16
/// bytes, which the linker keeps within a bank. Nothing is left that needs
/// a stack pointer.
void PIC16F1FrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *RS) const {
  const Function &F = MF.getFunction();
  if (!hasStaticFrame(MF))
    return;
  MachineFrameInfo &MFI = MF.getFrameInfo();
  auto *FuncInfo = MF.getInfo<PIC16F1MachineFunctionInfo>();

  BitVector Stays(MFI.getObjectIndexEnd());
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
    if (Size == 0)
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
  // Nothing may be left behind: there is no stack pointer to reach it with.
  if (Stays.any() || MFI.getNumFixedObjects() != 0 ||
      MFI.hasVarSizedObjects())
    report_fatal_error("PIC16F1: function '" + MF.getName() +
                       "' has a static frame but needs the stack; build "
                       "with -mllvm -pic16f1-static-frames=0");
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

/// With a static frame the saved registers have fixed addresses too: a
/// variable of their own, not slots in a frame.
bool PIC16F1FrameLowering::assignCalleeSavedSpillSlots(
    MachineFunction &MF, const TargetRegisterInfo *TRI,
    std::vector<CalleeSavedInfo> &CSI) const {
  if (!hasStaticFrame(MF))
    return false;
  if (CSI.empty())
    return true;
  const Function &F = MF.getFunction();
  Module &M = const_cast<Module &>(*F.getParent());
  Type *Ty = ArrayType::get(Type::getInt8Ty(M.getContext()), CSI.size());
  auto *GV = new GlobalVariable(M, Ty, /*isConstant=*/false,
                                GlobalValue::InternalLinkage,
                                Constant::getNullValue(Ty),
                                F.getName() + ".saved");
  GV->setAlignment(Align(1));
  GV->setSection((".noinit." + GV->getName()).str());
  MF.getInfo<PIC16F1MachineFunctionInfo>()->setSavedRegisters(GV);
  return true;
}

bool PIC16F1FrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return false;
  if (hasStaticFrame(*MBB.getParent())) {
    const PIC16F1InstrInfo &TII = *STI.getInstrInfo();
    const GlobalVariable *GV = MBB.getParent()
                                   ->getInfo<PIC16F1MachineFunctionInfo>()
                                   ->getSavedRegisters();
    int64_t At = 0;
    for (const CalleeSavedInfo &I : CSI) {
      if (!MBB.isLiveIn(I.getReg()))
        MBB.addLiveIn(I.getReg());
      BuildMI(MBB, MI, DebugLoc(), TII.get(PIC16F1::ST8abs))
          .addReg(I.getReg(), RegState::Kill)
          .addGlobalAddress(GV, At++);
    }
    return true;
  }
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
  if (hasStaticFrame(*MBB.getParent())) {
    const PIC16F1InstrInfo &TII = *STI.getInstrInfo();
    const GlobalVariable *GV = MBB.getParent()
                                   ->getInfo<PIC16F1MachineFunctionInfo>()
                                   ->getSavedRegisters();
    DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();
    int64_t At = 0;
    for (const CalleeSavedInfo &I : CSI)
      BuildMI(MBB, MI, DL, TII.get(PIC16F1::LD8abs), I.getReg())
          .addGlobalAddress(GV, At++);
    return true;
  }
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

bool PIC16F1FrameLowering::isProfitableForNoCSROpt(const Function &F) const {
  if (F.hasFnAttribute("pic16f1-dual-of"))
    return false;
  // A function with a variable-sized object keeps its frame base in RP6
  // and must save it for its caller like any callee-saved register.
  for (const BasicBlock &BB : F)
    for (const Instruction &I : BB)
      if (const auto *AI = dyn_cast<AllocaInst>(&I))
        if (!AI->isStaticAlloca())
          return false;
  return true;
}

/// Where a debugger finds the base of the frame: what FSR1 is while the
/// function's own code runs. FSR1 itself is not it after a call, until the
/// function next needs its frame and takes its stack pointer back. But a
/// function that makes calls has put that value in the cell __sp, and
/// every function it calls leaves the cell as it found it; one with
/// variable-sized objects, which move the cell, keeps it in RP6.
TargetFrameLowering::DwarfFrameBase
PIC16F1FrameLowering::getDwarfFrameBase(const MachineFunction &MF) const {
  DwarfFrameBase Base;
  Base.Kind = DwarfFrameBase::PIC16F1Base;
  if (pic16f1HasFrameBase(MF))
    Base.Location.Reg = PIC16F1::RP6;
  else if (MF.getFrameInfo().hasCalls() &&
           !MF.getFunction().hasFnAttribute("interrupt"))
    Base.Location.Reg = 0;
  else
    Base.Location.Reg = PIC16F1::SP;
  return Base;
}
