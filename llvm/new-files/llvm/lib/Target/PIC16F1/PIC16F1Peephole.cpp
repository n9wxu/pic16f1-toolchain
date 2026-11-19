//===-- PIC16F1Peephole.cpp - Clean up the final machine code -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Runs last, on real machine instructions. Pseudo expansion works one
// pseudo at a time, with every value passing through W into one of the byte
// registers in common RAM (0x70-0x7F); what one expansion leaves behind the
// next often does again. This pass looks at whole blocks and functions:
//
//  * It follows what W holds (a constant, or a copy of a byte register),
//    whether the Z flag describes W, and which byte registers are copies of
//    others, and removes loads and stores that change nothing.
//  * It reads a copy's original instead of the copy, which leaves the copy
//    unused.
//  * It computes which byte registers, W and flags are read later, over the
//    whole function, and removes instructions whose results are not.
//  * It turns "load a byte, store it in a register, test a bit of the
//    register" into a bit test of the byte.
//  * It follows which bank is selected, over the whole function, and
//    removes bank selections that change nothing.
//  * It turns a call followed by a return into a jump.
//  * It reads a byte of the stack frame again instead of a register it was
//    copied to, when that leaves the register unused.
//
// Only W, the Z and C flags, the sixteen bytes of common RAM and the two
// bytes of FSR0 are modelled; "byte register" below means one of those
// eighteen. Any other file register is memory, possibly a device register:
// an instruction that reads or writes one is never removed, and the bit-test
// change keeps it at exactly one read.
//
// An instruction that follows a skip (BTFSC, BTFSS, DECFSZ, INCFSZ) may or
// may not run. Such an instruction is never removed, and neither is a skip.
//
//===----------------------------------------------------------------------===//

#include "PIC16F1.h"
#include "PIC16F1InstrInfo.h"
#include "PIC16F1Subtarget.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/CommandLine.h"
#include <climits>

using namespace llvm;

#define DEBUG_TYPE "pic16f1-peephole"

static cl::opt<bool> EnablePeephole(
    "pic16f1-peephole", cl::Hidden, cl::init(true),
    cl::desc("Clean up the machine code after pseudo expansion"));

// For finding which step is at fault when something goes wrong.
static cl::opt<unsigned> Steps(
    "pic16f1-peephole-steps", cl::Hidden, cl::init(~0u),
    cl::desc("Bit mask of peephole steps: 1 read originals, 2 remove "
             "reloads, 4 reuse Z, 8 operate on originals, 16 remove dead "
             "instructions, 32 fold bit tests, 64 remove bank selections, "
             "128 tail calls, 256 skip on decrement and results in W, 512 "
             "reread stack bytes, 1024 constants in registers, 2048 step "
             "an FSR in the access"));

namespace {

// The modelled state, as bits: the sixteen bytes of common RAM, FSR0L and
// FSR0H, then W and the two flags.
enum : uint32_t {
  NumFiles = 18,
  CommonMask = 0xFFFF,
  FSR0Mask = 3u << 16,
  FilesMask = CommonMask | FSR0Mask,
  WBit = 1u << 18,
  ZBit = 1u << 19,
  CBit = 1u << 20,
  AllBits = FilesMask | WBit | ZBit | CBit,
};

/// The position of a modelled file register, or -1.
int indexOfFile(unsigned F) {
  F &= 0x7F;
  if (F >= 0x70)
    return F - 0x70;
  if (F == PIC16F1::FSR0L_ADDR || F == PIC16F1::FSR0H_ADDR)
    return 16 + (F - PIC16F1::FSR0L_ADDR);
  return -1;
}
unsigned fileOfIndex(int Idx) {
  return Idx < 16 ? 0x70 + Idx : PIC16F1::FSR0L_ADDR + (Idx - 16);
}

/// What an instruction does to the modelled state.
struct Effect {
  uint32_t Reads = 0;
  uint32_t Writes = 0;
  /// Not understood, or touches a core register this pass does not model:
  /// reads everything, and nothing is known afterwards.
  bool Barrier = false;
  /// Reads or writes memory outside the model. Never removed.
  bool Memory = false;
  /// Makes the next instruction conditional.
  bool Skip = false;
  /// The position of the byte register the instruction names, or -1.
  int File = -1;
  /// The result goes to W (and Z, if written, describes W afterwards).
  bool ResultInW = false;
};

/// The bit for a file operand, 0 for memory outside the model, or ~0u for
/// a core register that makes the instruction a barrier. STATUS is left
/// to the caller.
uint32_t fileBits(const MachineOperand &MO) {
  if (!MO.isImm())
    return 0;
  unsigned F = MO.getImm() & 0x7F;
  if (int Idx = indexOfFile(F); Idx >= 0)
    return 1u << Idx;
  // INDF0 (the byte FSR0 points at; none of the code generator's own
  // instructions name it), PCL (a write jumps), STATUS, and WREG (W itself).
  if (F == 0x00 || F == PIC16F1::PCL_ADDR || F == PIC16F1::STATUS_ADDR ||
      F == 0x09)
    return ~0u;
  return 0;
}

/// The byte registers that make up a register, as bits.
uint32_t registerFiles(const MachineInstr &MI, MCRegister Reg) {
  const MachineFunction &MF = *MI.getMF();
  const auto &TII = *MF.getSubtarget<PIC16F1Subtarget>().getInstrInfo();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  uint32_t Bits = 0;
  for (MCPhysReg Sub : TRI.subregs_inclusive(Reg)) {
    if (!PIC16F1::GPR8RegClass.contains(Sub) && Sub != PIC16F1::TMP0 &&
        Sub != PIC16F1::TMP1)
      continue;
    if (int Idx = indexOfFile(TII.fileAddr(Sub)); Idx >= 0)
      Bits |= 1u << Idx;
  }
  return Bits;
}

/// The byte registers a call may change, by its register mask.
uint32_t clobberedFiles(const MachineInstr &MI, const MachineOperand &Mask) {
  uint32_t Bits = 0;
  for (MCPhysReg Reg : PIC16F1::GPR8RegClass)
    if (Mask.clobbersPhysReg(Reg))
      Bits |= registerFiles(MI, Reg);
  for (MCPhysReg Reg : {PIC16F1::TMP0, PIC16F1::TMP1})
    Bits |= registerFiles(MI, Reg);
  return Bits;
}

Effect effectOf(const MachineInstr &MI) {
  using namespace PIC16F1;
  Effect E;
  auto Barrier = [&] {
    E = Effect();
    E.Barrier = true;
    return E;
  };
  auto File = [&](const MachineOperand &MO) -> uint32_t {
    uint32_t Bits = fileBits(MO);
    if (Bits == 0)
      E.Memory = true;
    else if (Bits != ~0u)
      E.File = indexOfFile(MO.getImm());
    return Bits;
  };

  unsigned Opc = MI.getOpcode();
  switch (Opc) {
  // file OP W, result to the file or to W.
  case ADDWF:
  case ADDWFC:
  case ANDWF:
  case IORWF:
  case XORWF:
  case SUBWF:
  case SUBWFB:
  // One operand.
  case ASRF:
  case LSLF:
  case LSRF:
  case COMF:
  case DECF:
  case INCF:
  case MOVF:
  case RLF:
  case RRF:
  case SWAPF:
  case DECFSZ:
  case INCFSZ: {
    uint32_t F = File(MI.getOperand(0));
    if (F == ~0u)
      return Barrier();
    bool ToFile = MI.getOperand(1).getImm() != 0;
    E.Reads = F;
    bool UsesW = Opc == ADDWF || Opc == ADDWFC || Opc == ANDWF ||
                 Opc == IORWF || Opc == XORWF || Opc == SUBWF || Opc == SUBWFB;
    if (UsesW)
      E.Reads |= WBit;
    if (Opc == ADDWFC || Opc == SUBWFB || Opc == RLF || Opc == RRF)
      E.Reads |= CBit;
    if (ToFile) {
      E.Writes = F;
      // Writing memory outside the model.
      if (F == 0)
        E.Memory = true;
    } else {
      E.Writes = WBit;
      E.ResultInW = true;
    }
    switch (Opc) {
    case ADDWF:
    case ADDWFC:
    case SUBWF:
    case SUBWFB:
    case ASRF:
    case LSLF:
    case LSRF:
      E.Writes |= ZBit | CBit;
      break;
    case ANDWF:
    case IORWF:
    case XORWF:
    case COMF:
    case DECF:
    case INCF:
    case MOVF:
      E.Writes |= ZBit;
      break;
    case RLF:
    case RRF:
      E.Writes |= CBit;
      break;
    case DECFSZ:
    case INCFSZ:
      E.Skip = true;
      break;
    default:
      break;
    }
    return E;
  }
  case CLRF: {
    uint32_t F = File(MI.getOperand(0));
    if (F == ~0u)
      return Barrier();
    E.Writes = F | ZBit;
    return E;
  }
  case MOVWF: {
    uint32_t F = File(MI.getOperand(0));
    if (F == ~0u)
      return Barrier();
    E.Reads = WBit;
    E.Writes = F;
    return E;
  }
  case CLRW:
    E.Writes = WBit | ZBit;
    E.ResultInW = true;
    return E;
  case BCF:
  case BSF:
  case BTFSC:
  case BTFSS: {
    const MachineOperand &FO = MI.getOperand(0);
    unsigned Bit = MI.getOperand(1).getImm();
    bool Test = Opc == BTFSC || Opc == BTFSS;
    E.Skip = Test;
    if (FO.isImm() && (FO.getImm() & 0x7F) == STATUS_ADDR) {
      // Bit 0 is the carry flag, bit 2 the zero flag.
      uint32_t Flag = Bit == 0 ? CBit : Bit == 2 ? ZBit : 0;
      if (!Flag)
        return Barrier();
      if (Test)
        E.Reads = Flag;
      else
        E.Writes = Flag;
      return E;
    }
    uint32_t F = File(FO);
    if (F == ~0u)
      return Barrier();
    E.Reads = F;
    if (!Test)
      E.Writes = F;
    return E;
  }
  case ADDLW:
  case SUBLW:
    E.Reads = WBit;
    E.Writes = WBit | ZBit | CBit;
    E.ResultInW = true;
    return E;
  case ANDLW:
  case IORLW:
  case XORLW:
    E.Reads = WBit;
    E.Writes = WBit | ZBit;
    E.ResultInW = true;
    return E;
  case MOVLW:
    E.Writes = WBit;
    return E;
  // The bank and page registers and FSR1 are outside the model.
  case MOVLB:
  case MOVLB6:
  case MOVLP:
    E.Memory = true;
    return E;
  case ADDFSR:
    E.Memory = true;
    if (MI.getOperand(0).getImm() == 0) {
      E.Reads = FSR0Mask;
      E.Writes = FSR0Mask;
    }
    return E;
  case MOVIWm:
  case MOVWIm: {
    // With pre- or post-increment or -decrement; bit 2 selects FSR1.
    E.Memory = true;
    if (!(MI.getOperand(0).getImm() & 4)) {
      E.Reads = FSR0Mask;
      E.Writes = FSR0Mask;
    }
    if (Opc == MOVIWm) {
      E.Writes |= WBit | ZBit;
      E.ResultInW = true;
    } else {
      E.Reads |= WBit;
    }
    return E;
  }
  case MOVIWk:
  case MOVWIk:
    // A load from the stack frame (FSR1) has no other effect and may be
    // removed. One through FSR0 may read a device register.
    E.Memory = Opc == MOVWIk || MI.getOperand(1).getImm() == 0;
    if (MI.getOperand(1).getImm() == 0)
      E.Reads = FSR0Mask;
    if (Opc == MOVIWk) {
      E.Writes = WBit | ZBit;
      E.ResultInW = true;
    } else {
      E.Reads |= WBit;
    }
    return E;
  case BRA:
    return E;
  case GOTO: {
    if (MI.getOperand(0).isMBB())
      return E;
    // A jump to another function: a call and a return in one.
    E.Memory = true;
    const MachineFunction &MF = *MI.getMF();
    for (const MachineOperand &MO : MI.operands())
      if (MO.isReg() && MO.isUse())
        E.Reads |= registerFiles(MI, MO.getReg());
    const MCPhysReg *Saved =
        MF.getSubtarget().getRegisterInfo()->getCalleeSavedRegs(&MF);
    for (; Saved && *Saved; ++Saved)
      E.Reads |= registerFiles(MI, *Saved);
    return E;
  }
  case NOP:
  case SLEEP:
  case CLRWDT:
    E.Memory = true;
    return E;
  case CALL: {
    // Arguments are in byte registers, never in W; the instruction lists
    // them. W, the flags and the registers the callee need not keep do
    // not survive the call.
    E.Memory = true;
    E.Writes = WBit | ZBit | CBit;
    const MachineOperand *Mask = nullptr;
    for (const MachineOperand &MO : MI.operands()) {
      if (MO.isRegMask())
        Mask = &MO;
      else if (MO.isReg() && MO.isUse())
        E.Reads |= registerFiles(MI, MO.getReg());
    }
    if (!Mask) {
      E.Reads = FilesMask;
      return E;
    }
    E.Writes |= clobberedFiles(MI, *Mask);
    return E;
  }
  case RETURN: {
    // The result, which the instruction lists, and the registers the
    // caller expects back unchanged.
    E.Memory = true;
    const MachineFunction &MF = *MI.getMF();
    for (const MachineOperand &MO : MI.operands())
      if (MO.isReg() && MO.isUse())
        E.Reads |= registerFiles(MI, MO.getReg());
    const MCPhysReg *Saved =
        MF.getSubtarget().getRegisterInfo()->getCalleeSavedRegs(&MF);
    for (; Saved && *Saved; ++Saved)
      E.Reads |= registerFiles(MI, *Saved);
    return E;
  }
  case RETFIE:
    // Common RAM belongs to the interrupted code. FSR0, like W and the
    // flags, comes back from the shadow registers.
    E.Reads = CommonMask;
    E.Memory = true;
    return E;
  default:
    // Inline assembly may do anything, except for the single instructions
    // that device headers wrap in macros and that touch nothing modelled.
    if (MI.isInlineAsm() && MI.getOperand(0).isSymbol()) {
      StringRef Asm = StringRef(MI.getOperand(0).getSymbolName()).trim();
      if (Asm.equals_insensitive("sleep") || Asm.equals_insensitive("nop") ||
          Asm.equals_insensitive("clrwdt")) {
        E.Memory = true;
        return E;
      }
    }
    return Barrier();
  }
}

class PIC16F1Peephole : public MachineFunctionPass {
public:
  static char ID;
  PIC16F1Peephole() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return "PIC16F1 peephole"; }

private:
  /// For each block, the state that is read after its last instruction.
  DenseMap<const MachineBasicBlock *, uint32_t> LiveOut;
  /// For each instruction, the state that is read after it.
  DenseMap<const MachineInstr *, uint32_t> LiveAfter;

  static bool isReal(const MachineInstr &MI) { return !MI.isMetaInstruction(); }
  /// True if the instruction before \p MI is a skip, so \p MI may not run.
  static bool isConditional(const MachineInstr &MI);
  static uint32_t transfer(const MachineInstr &MI, uint32_t Live);

  void computeLiveness(MachineFunction &MF);
  bool removeDead(MachineBasicBlock &MBB);
  bool forward(MachineBasicBlock &MBB);
  bool stepPointer(MachineBasicBlock &MBB);
  bool removeBankSelects(MachineFunction &MF);
  bool tailCalls(MachineFunction &MF);
};

} // end anonymous namespace

char PIC16F1Peephole::ID = 0;

INITIALIZE_PASS(PIC16F1Peephole, DEBUG_TYPE, "PIC16F1 peephole", false, false)

FunctionPass *llvm::createPIC16F1PeepholePass() { return new PIC16F1Peephole(); }

/// The real instruction before \p MI, looking into the block laid out before
/// this one if \p MI is the first: a skip there acts on \p MI just the same.
static const MachineInstr *previousReal(const MachineInstr &MI) {
  const MachineBasicBlock *MBB = MI.getParent();
  // getReverseIterator points at MI itself. (A reverse iterator made from
  // MI's forward iterator would already point one before it.)
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

bool PIC16F1Peephole::isConditional(const MachineInstr &MI) {
  const MachineInstr *Prev = previousReal(MI);
  if (!Prev)
    return false;
  // Inline assembly may end in a skip.
  return effectOf(*Prev).Skip || Prev->isInlineAsm();
}

/// The state read before \p MI, given what is read after it.
uint32_t PIC16F1Peephole::transfer(const MachineInstr &MI, uint32_t Live) {
  Effect E = effectOf(MI);
  if (E.Barrier)
    return AllBits;
  if (isConditional(MI))
    return Live | E.Reads;
  return (Live & ~E.Writes) | E.Reads;
}

void PIC16F1Peephole::computeLiveness(MachineFunction &MF) {
  LiveOut.clear();
  LiveAfter.clear();
  DenseMap<const MachineBasicBlock *, uint32_t> LiveIn;
  bool Changed = true;
  while (Changed) {
    Changed = false;
    for (MachineBasicBlock *MBB : post_order(&MF)) {
      uint32_t Live = 0;
      for (const MachineBasicBlock *Succ : MBB->successors())
        Live |= LiveIn.lookup(Succ);
      LiveOut[MBB] = Live;
      for (const MachineInstr &MI : llvm::reverse(*MBB))
        if (isReal(MI))
          Live = transfer(MI, Live);
      uint32_t &In = LiveIn[MBB];
      if (In != Live) {
        In = Live;
        Changed = true;
      }
    }
  }
  // Blocks no path from the entry reaches are not in the post-order; they
  // keep everything.
  for (MachineBasicBlock &MBB : MF) {
    uint32_t Live = LiveOut.count(&MBB) ? LiveOut[&MBB] : uint32_t(AllBits);
    LiveOut[&MBB] = Live;
    for (const MachineInstr &MI : llvm::reverse(MBB)) {
      if (!isReal(MI))
        continue;
      LiveAfter[&MI] = Live;
      Live = transfer(MI, Live);
    }
  }
}

/// Remove instructions whose results nothing reads, and fold a load, a
/// store and a bit test into the bit test.
bool PIC16F1Peephole::removeDead(MachineBasicBlock &MBB) {
  using namespace PIC16F1;
  bool Changed = false;
  uint32_t Live = LiveOut[&MBB];
  for (MachineBasicBlock::reverse_iterator It = MBB.rbegin();
       It != MBB.rend();) {
    MachineInstr &MI = *It++;
    if (!isReal(MI))
      continue;
    Effect E = effectOf(MI);
    bool Cond = isConditional(MI);

    // movf M, W ; movwf T ; btfsx T, b   =>   btfsx M, b
    // when T, W and Z are not read afterwards. The bank M needs is
    // selected already: nothing stands between the three.
    if ((Steps & 32) && (MI.getOpcode() == BTFSC || MI.getOpcode() == BTFSS) &&
        E.File >= 0 &&
        !(Live & ((1u << E.File) | WBit | ZBit))) {
      MachineInstr *Store = nullptr, *Load = nullptr;
      if (It != MBB.rend() && It->getOpcode() == MOVWF &&
          It->getOperand(0).isImm() &&
          indexOfFile(It->getOperand(0).getImm()) == E.File)
        Store = &*It;
      if (Store && std::next(It) != MBB.rend() &&
          std::next(It)->getOpcode() == MOVF &&
          std::next(It)->getOperand(1).getImm() == 0)
        Load = &*std::next(It);
      if (Load && !isConditional(*Load) && !effectOf(*Load).Barrier &&
          (Load->getOperand(0).isImm() || Load->getOperand(0).isGlobal())) {
        const MachineOperand &Src = Load->getOperand(0);
        if (Src.isImm())
          MI.getOperand(0).setImm(Src.getImm());
        else
          MI.getOperand(0).ChangeToGA(Src.getGlobal(), Src.getOffset(),
                                      Src.getTargetFlags());
        It = std::next(std::next(It));
        Store->eraseFromParent();
        Load->eraseFromParent();
        Changed = true;
        Live = transfer(MI, Live);
        continue;
      }
    }

    // movf M, W ; movwf T ; movlw K ; OP T, W   =>   movlw K ; OP M, W
    // (or with a register loaded in place of the constant) when T is not
    // read afterwards: the memory byte M is the operand, still read exactly
    // once, and the bank it needs is still selected.
    if ((Steps & 256) && !Cond && E.File >= 0 && E.ResultInW &&
        (MI.getOpcode() == ADDWF || MI.getOpcode() == ANDWF ||
         MI.getOpcode() == IORWF || MI.getOpcode() == XORWF ||
         MI.getOpcode() == SUBWF) &&
        !(Live & (1u << E.File))) {
      auto Other = It;
      MachineInstr *Load = nullptr, *Store = nullptr;
      if (Other != MBB.rend() &&
          (Other->getOpcode() == MOVLW ||
           (Other->getOpcode() == MOVF && Other->getOperand(0).isImm() &&
            Other->getOperand(1).getImm() == 0 &&
            indexOfFile(Other->getOperand(0).getImm()) >= 0 &&
            indexOfFile(Other->getOperand(0).getImm()) != E.File))) {
        auto S = std::next(Other);
        if (S != MBB.rend() && S->getOpcode() == MOVWF &&
            S->getOperand(0).isImm() &&
            indexOfFile(S->getOperand(0).getImm()) == E.File) {
          Store = &*S;
          auto L = std::next(S);
          if (L != MBB.rend() && L->getOpcode() == MOVF &&
              L->getOperand(1).getImm() == 0 &&
              (L->getOperand(0).isGlobal() ||
               (L->getOperand(0).isImm() &&
                fileBits(L->getOperand(0)) == 0)) &&
              !isConditional(*L))
            Load = &*L;
        }
      }
      if (Load) {
        const MachineOperand &Src = Load->getOperand(0);
        if (Src.isImm())
          MI.getOperand(0).setImm(Src.getImm());
        else
          MI.getOperand(0).ChangeToGA(Src.getGlobal(), Src.getOffset(),
                                      Src.getTargetFlags());
        Store->eraseFromParent();
        Load->eraseFromParent();
        Changed = true;
        Live = transfer(MI, Live);
        continue;
      }
    }

    // decf F, F ; btfss STATUS, Z   =>   decfsz F, F
    // (and the same for INCF) when Z is not read afterwards: DECFSZ leaves
    // the flags alone.
    if ((Steps & 256) && MI.getOpcode() == BTFSS && MI.getOperand(0).isImm() &&
        (MI.getOperand(0).getImm() & 0x7F) == STATUS_ADDR &&
        MI.getOperand(1).getImm() == STATUS_Z && !(Live & ZBit) &&
        It != MBB.rend() &&
        (It->getOpcode() == DECF || It->getOpcode() == INCF) &&
        It->getOperand(1).getImm() == 1 && !isConditional(*It)) {
      const PIC16F1InstrInfo &TII =
          *MBB.getParent()->getSubtarget<PIC16F1Subtarget>().getInstrInfo();
      It->setDesc(TII.get(It->getOpcode() == DECF ? DECFSZ : INCFSZ));
      MI.eraseFromParent();
      Changed = true;
      continue;
    }

    // OP F, F ; movf F, W   =>   OP F, W
    // when F is not read afterwards. Z comes out the same, except after
    // the operations that leave it alone; then it must not be read.
    if ((Steps & 256) && MI.getOpcode() == MOVF && E.File >= 0 && !Cond &&
        MI.getOperand(1).getImm() == 0 && !(Live & (1u << E.File)) &&
        It != MBB.rend() && It->getNumExplicitOperands() == 2 &&
        It->getOperand(0).isImm() && It->getOperand(1).isImm() &&
        It->getOperand(1).getImm() == 1 && !isConditional(*It)) {
      bool SetsZ = false, Fits = false;
      switch (It->getOpcode()) {
      case ADDWF: case ADDWFC: case ANDWF: case IORWF: case XORWF: case SUBWF:
      case SUBWFB: case ASRF: case LSLF: case LSRF: case COMF: case DECF:
      case INCF:
        SetsZ = Fits = true;
        break;
      case RLF: case RRF: case SWAPF:
        Fits = true;
        break;
      default:
        break;
      }
      if (Fits && indexOfFile(It->getOperand(0).getImm()) == E.File &&
          fileBits(It->getOperand(0)) != ~0u && (SetsZ || !(Live & ZBit))) {
        It->getOperand(1).setImm(0);
        MI.eraseFromParent();
        Changed = true;
        continue;
      }
    }

    if ((Steps & 16) && !Cond && !E.Barrier && !E.Memory && !E.Skip &&
        !MI.isTerminator() &&
        E.Writes != 0 && !(E.Writes & Live)) {
      MI.eraseFromParent();
      Changed = true;
      continue;
    }
    Live = transfer(MI, Live);
  }
  return Changed;
}

/// Follow what W, Z and the byte registers hold through a block.
bool PIC16F1Peephole::forward(MachineBasicBlock &MBB) {
  using namespace PIC16F1;
  bool Changed = false;

  int WConst = -1;        // W is this constant
  uint32_t WEq = 0;       // W equals each of these byte registers
  bool ZIsW = false;      // Z is set exactly when W is zero
  int ZFile = -1;         // Z is set exactly when this byte register is zero
  int CopyOf[NumFiles];   // the register each byte register is a copy of
  for (int I = 0; I != NumFiles; ++I)
    CopyOf[I] = I;
  int FileConst[NumFiles]; // the byte register holds this constant, or -1
  for (int I = 0; I != NumFiles; ++I)
    FileConst[I] = -1;
  // Bytes of the stack frame, by their offset from FSR1.
  const int NoSlot = INT_MIN;
  int WSlot = NoSlot;     // W equals this byte of the frame
  int SlotOf[NumFiles];   // the byte register equals this byte of the frame
  for (int I = 0; I != NumFiles; ++I)
    SlotOf[I] = NoSlot;
  auto ForgetSlots = [&] {
    WSlot = NoSlot;
    for (int I = 0; I != NumFiles; ++I)
      SlotOf[I] = NoSlot;
  };

  auto ForgetW = [&] {
    WConst = -1;
    WEq = 0;
    ZIsW = false;
    WSlot = NoSlot;
  };
  auto ForgetFile = [&](int Idx) {
    WEq &= ~(1u << Idx);
    if (ZFile == Idx)
      ZFile = -1;
    // Its copies are still copies of one another, but that is not kept.
    for (int I = 0; I != NumFiles; ++I)
      if (CopyOf[I] == Idx)
        CopyOf[I] = I;
    CopyOf[Idx] = Idx;
    SlotOf[Idx] = NoSlot;
    FileConst[Idx] = -1;
  };
  auto ForgetAll = [&] {
    ForgetW();
    ZFile = -1;
    for (int I = 0; I != NumFiles; ++I) {
      CopyOf[I] = I;
      FileConst[I] = -1;
    }
    ForgetSlots();
  };

  for (MachineBasicBlock::iterator It = MBB.begin(); It != MBB.end();) {
    MachineInstr &MI = *It++;
    if (!isReal(MI))
      continue;
    Effect E = effectOf(MI);
    if (E.Barrier) {
      ForgetAll();
      continue;
    }
    bool Cond = isConditional(MI);
    unsigned Opc = MI.getOpcode();
    int Idx = E.File;
    uint32_t After = LiveAfter.lookup(&MI);

    // The last read of a register that holds a byte of the stack frame:
    // read the frame instead, which leaves the register unread. (MOVIW
    // sets Z as MOVF does.)
    if ((Steps & 512) && Opc == MOVF && Idx >= 0 &&
        MI.getOperand(1).getImm() == 0 && SlotOf[Idx] != NoSlot &&
        !(After & (1u << Idx))) {
      const PIC16F1InstrInfo &TII =
          *MBB.getParent()->getSubtarget<PIC16F1Subtarget>().getInstrInfo();
      int Slot = SlotOf[Idx];
      MI.setDesc(TII.get(MOVIWk));
      MI.getOperand(0).setImm(Slot);
      MI.getOperand(1).setImm(1);
      E = effectOf(MI);
      Opc = MOVIWk;
      Idx = -1;
      Changed = true;
    }

    // A register that holds a known constant: load the constant. That is
    // no shorter, but W often holds it already, and the register may then
    // go unread. MOVLW leaves Z alone, so Z must not be read.
    if ((Steps & 1024) && !Cond && Opc == MOVF && Idx >= 0 &&
        MI.getOperand(1).getImm() == 0 && FileConst[Idx] >= 0 &&
        !(After & ZBit)) {
      const PIC16F1InstrInfo &TII =
          *MBB.getParent()->getSubtarget<PIC16F1Subtarget>().getInstrInfo();
      int K = FileConst[Idx];
      MI.setDesc(TII.get(MOVLW));
      MI.removeOperand(1);
      MI.getOperand(0).setImm(K);
      E = effectOf(MI);
      Opc = MOVLW;
      Idx = -1;
      Changed = true;
    }
    // movlw 0 ; movwf F   =>   clrf F   when W and Z are not read after.
    if ((Steps & 1024) && !Cond && Opc == MOVWF && WConst == 0 &&
        !(After & (WBit | ZBit)) &&
        (MI.getOperand(0).isImm() ? fileBits(MI.getOperand(0)) != ~0u
                                  : MI.getOperand(0).isGlobal())) {
      const PIC16F1InstrInfo &TII =
          *MBB.getParent()->getSubtarget<PIC16F1Subtarget>().getInstrInfo();
      MI.setDesc(TII.get(CLRF));
      E = effectOf(MI);
      Opc = CLRF;
      Changed = true;
    }

    // Read the original instead of a copy.
    if ((Steps & 1) && Idx >= 0 && (E.Reads & (1u << Idx)) &&
        !(E.Writes & (1u << Idx)) && CopyOf[Idx] != Idx) {
      Idx = CopyOf[Idx];
      MI.getOperand(0).setImm(fileOfIndex(Idx));
      E = effectOf(MI);
      Changed = true;
    }

    if (!Cond && (Steps & 2)) {
      bool Remove = false;
      // W already holds the register. Z would be set from the same value;
      // if it does not describe W now, it must not be read later.
      if (Opc == MOVF && Idx >= 0 && !E.Memory &&
          MI.getOperand(1).getImm() == 0 && (WEq & (1u << Idx)) &&
          (ZIsW || !(After & ZBit)))
        Remove = true;
      // Z already tells whether the register is zero, and that is all
      // the instruction is for.
      if ((Steps & 4) && Opc == MOVF && Idx >= 0 && !E.Memory && ZFile == Idx &&
          (MI.getOperand(1).getImm() != 0 || !(After & WBit)))
        Remove = true;
      // W already holds the constant.
      if (Opc == MOVLW && MI.getOperand(0).isImm() && WConst >= 0 &&
          WConst == int(MI.getOperand(0).getImm() & 0xFF))
        Remove = true;
      // The register already holds W.
      if (Opc == MOVWF && Idx >= 0 && (WEq & (1u << Idx)))
        Remove = true;
      if (Remove) {
        MI.eraseFromParent();
        Changed = true;
        continue;
      }
    }

    // An operation in place on a register that is still an unchanged copy:
    //     movf G, W ; movwf F ; ... ; OP F, F
    // Do the operation on the original, with the result in W, and store
    // that:  OP G, W ; movwf F.  The copy is then not read and goes away.
    // W holds the result afterwards instead of what it held, so it must
    // not be read.
    if ((Steps & 8) && !Cond && Idx >= 0 && CopyOf[Idx] != Idx &&
        !(After & WBit) &&
        (E.Writes & (1u << Idx)) && MI.getNumExplicitOperands() == 2 &&
        MI.getOperand(1).isImm() && MI.getOperand(1).getImm() == 1 &&
        Opc != MOVF && Opc != DECFSZ && Opc != INCFSZ && Opc != BCF &&
        Opc != BSF) {
      const PIC16F1InstrInfo &TII =
          *MBB.getParent()->getSubtarget<PIC16F1Subtarget>().getInstrInfo();
      MI.getOperand(0).setImm(fileOfIndex(CopyOf[Idx]));
      MI.getOperand(1).setImm(0);
      MachineInstr *Store = BuildMI(MBB, It, MI.getDebugLoc(), TII.get(MOVWF))
                                .addImm(fileOfIndex(Idx));
      // Until the next round, treat what follows the store as following
      // the operation.
      LiveAfter[Store] = After;
      ForgetFile(Idx);
      WConst = -1;
      WSlot = NoSlot;
      WEq = 1u << Idx;
      ZIsW = (E.Writes & ZBit) != 0;
      ZFile = ZIsW ? Idx : (E.Writes & ZBit) ? -1 : ZFile;
      It = std::next(Store->getIterator());
      Changed = true;
      continue;
    }

    // Update what is known. First what is known of the stack frame. It
    // survives instructions that only read, and ones that write a register
    // or a variable; anything else may move FSR1 or store into the frame.
    bool FrameLoad = Opc == MOVIWk && MI.getOperand(1).getImm() == 1;
    bool FrameStore = Opc == MOVWIk && MI.getOperand(1).getImm() == 1;
    bool Keeps = !E.Memory || Opc == MOVIWk || Opc == MOVLB || Opc == MOVLB6 ||
                 Opc == MOVLP || Opc == BTFSC || Opc == BTFSS ||
                 (E.ResultInW && Opc != MOVIWm) ||
                 (MI.getNumExplicitOperands() >= 1 &&
                  MI.getOperand(0).isGlobal() && Opc != CALL && Opc != GOTO);
    if (FrameStore) {
      int Slot = MI.getOperand(0).getImm();
      for (int I = 0; I != NumFiles; ++I)
        if (SlotOf[I] == Slot)
          SlotOf[I] = NoSlot;
      // W is what the byte now holds, if the store ran.
      WSlot = Cond ? NoSlot : Slot;
    } else if (!Keeps) {
      ForgetSlots();
    }
    // A register loaded with W equals the frame byte that W came from.
    int WroteIdx = Idx >= 0 && (E.Writes & (1u << Idx)) ? Idx : -1;
    int NewSlot = !Cond && Opc == MOVWF ? WSlot : NoSlot;
    int NewConst = Cond ? -1 : Opc == CLRF ? 0 : Opc == MOVWF ? WConst : -1;
    int NewWSlot = WSlot;
    if (E.Writes & WBit) {
      NewWSlot = NoSlot;
      if (!Cond && FrameLoad)
        NewWSlot = MI.getOperand(0).getImm();
      else if (!Cond && Opc == MOVF && Idx >= 0)
        NewWSlot = SlotOf[Idx];
    }

    auto UpdateRest = [&] {
      // Then everything else; first the byte registers the instruction
      // changes without naming them (FSR0, by ADDFSR or an auto-increment).
      for (uint32_t Rest = E.Writes & FilesMask & ~(Idx >= 0 ? 1u << Idx : 0u);
           Rest; Rest &= Rest - 1)
        ForgetFile(llvm::countr_zero(Rest));
      if (Cond) {
        if (E.Writes & WBit)
          ForgetW();
        if (E.Writes & ZBit) {
          ZIsW = false;
          ZFile = -1;
        }
        if (Idx >= 0 && (E.Writes & (1u << Idx)))
          ForgetFile(Idx);
        return;
      }
      if (Opc == CALL) {
        ForgetAll();
        return;
      }
      if (E.Writes & WBit) {
        WConst = -1;
        WEq = 0;
        if (Opc == MOVLW) {
          if (MI.getOperand(0).isImm())
            WConst = MI.getOperand(0).getImm() & 0xFF;
          ZIsW = false;
        } else {
          if (Opc == MOVF && Idx >= 0) {
            // W equals the register and every register it is a copy of or
            // that is a copy of the same original.
            int Root = CopyOf[Idx];
            for (int I = 0; I != NumFiles; ++I)
              if (CopyOf[I] == Root)
                WEq |= 1u << I;
          }
          if (Opc == CLRW)
            WConst = 0;
          ZIsW = (E.Writes & ZBit) != 0;
          if (E.Writes & ZBit)
            ZFile = Opc == MOVF && Idx >= 0 ? Idx : -1;
        }
        return;
      }
      if (E.Writes & ZBit) {
        ZIsW = false;
        ZFile = -1;
      }
      if (Idx >= 0 && (E.Writes & (1u << Idx))) {
        if (Opc == MOVWF) {
          // The register is now a copy of whatever W was loaded from.
          int Root = -1;
          for (int I = 0; I != NumFiles && Root < 0; ++I)
            if (I != Idx && (WEq & (1u << I)))
              Root = CopyOf[I];
          ForgetFile(Idx);
          if (Root >= 0 && Root != Idx)
            CopyOf[Idx] = Root;
          WEq |= 1u << Idx;
          if (ZIsW)
            ZFile = Idx;
        } else {
          ForgetFile(Idx);
          // The result is in the register, and Z describes it. (RLF, RRF
          // and SWAPF leave Z alone; they are not in this group.)
          if (E.Writes & ZBit)
            ZFile = Idx;
        }
      }
    };
    UpdateRest();
    WSlot = NewWSlot;
    if (WroteIdx >= 0) {
      SlotOf[WroteIdx] = NewSlot;
      FileConst[WroteIdx] = NewConst;
    }
  }
  return Changed;
}

namespace {
/// What is known about BSR at one point.
struct BankState {
  enum KindTy { Top, Unknown, Const, Global } Kind = Top;
  int64_t Value = 0; // bank number, or offset into the global
  const GlobalValue *GV = nullptr;

  bool operator==(const BankState &O) const {
    return Kind == O.Kind && Value == O.Value && GV == O.GV;
  }
  static BankState unknown() {
    BankState S;
    S.Kind = Unknown;
    return S;
  }
  /// What both paths agree on.
  BankState meet(const BankState &O) const {
    if (Kind == Top)
      return O;
    if (O.Kind == Top || *this == O)
      return *this;
    return unknown();
  }
};

/// The bank a MOVLB selects.
BankState bankOf(const MachineInstr &MI) {
  BankState S;
  const MachineOperand &MO = MI.getOperand(0);
  if (MO.isImm()) {
    S.Kind = BankState::Const;
    S.Value = MO.getImm();
  } else if (MO.isGlobal()) {
    S.Kind = BankState::Global;
    S.GV = MO.getGlobal();
    // Every byte of a small variable is in one bank.
    S.Value = PIC16F1InstrInfo::inOneBank(S.GV, MO.getOffset())
                  ? 0
                  : MO.getOffset();
  } else {
    S.Kind = BankState::Unknown;
  }
  return S;
}

/// BSR after \p MI, given BSR before it.
BankState bankAfter(const MachineInstr &MI, BankState S, bool Cond) {
  using namespace PIC16F1;
  switch (MI.getOpcode()) {
  case MOVLB:
  case MOVLB6: {
    BankState New = bankOf(MI);
    // After a skip it may not have run.
    return Cond ? S.meet(New) : New;
  }
  // Instructions that leave BSR alone, unless they name it.
  case ADDWF: case ADDWFC: case ANDWF: case IORWF: case XORWF: case SUBWF:
  case SUBWFB: case ASRF: case LSLF: case LSRF: case COMF: case DECF:
  case INCF: case MOVF: case RLF: case RRF: case SWAPF: case DECFSZ:
  case INCFSZ: case CLRF: case MOVWF: case BCF: case BSF: case BTFSC:
  case BTFSS:
    if (MI.getOperand(0).isImm() && (MI.getOperand(0).getImm() & 0x7F) == 0x08)
      return BankState::unknown();
    return S;
  case CLRW: case ADDLW: case SUBLW: case ANDLW: case IORLW: case XORLW:
  case MOVLW: case MOVLP: case ADDFSR: case MOVIWm: case MOVIWk: case MOVWIm:
  case MOVWIk: case BRA: case GOTO: case NOP: case SLEEP: case CLRWDT:
  case RETURN: case RETFIE:
    return S;
  default:
    // Calls, inline assembly and anything not understood.
    return BankState::unknown();
  }
}
} // end anonymous namespace

/// Remove MOVLB instructions that select the bank already selected on every
/// path to them.
bool PIC16F1Peephole::removeBankSelects(MachineFunction &MF) {
  DenseMap<const MachineBasicBlock *, BankState> Out;
  auto InOf = [&](const MachineBasicBlock &MBB) {
    if (&MBB == &MF.front() || MBB.isEHPad() || MBB.hasAddressTaken())
      return BankState::unknown();
    BankState S;
    for (const MachineBasicBlock *Pred : MBB.predecessors())
      S = S.meet(Out.lookup(Pred));
    return S;
  };
  bool Changed = true;
  while (Changed) {
    Changed = false;
    for (MachineBasicBlock &MBB : MF) {
      BankState S = InOf(MBB);
      for (const MachineInstr &MI : MBB)
        if (isReal(MI))
          S = bankAfter(MI, S, isConditional(MI));
      BankState &O = Out[&MBB];
      if (!(O == S)) {
        O = S;
        Changed = true;
      }
    }
  }
  bool Removed = false;
  for (MachineBasicBlock &MBB : MF) {
    BankState S = InOf(MBB);
    // A block nothing reaches.
    if (S.Kind == BankState::Top)
      continue;
    for (MachineBasicBlock::iterator It = MBB.begin(); It != MBB.end();) {
      MachineInstr &MI = *It++;
      if (!isReal(MI))
        continue;
      bool Cond = isConditional(MI);
      if ((MI.getOpcode() == PIC16F1::MOVLB ||
           MI.getOpcode() == PIC16F1::MOVLB6) &&
          !Cond && S.Kind != BankState::Unknown && bankOf(MI) == S) {
        MI.eraseFromParent();
        Removed = true;
        continue;
      }
      S = bankAfter(MI, S, Cond);
    }
  }
  return Removed;
}

/// call f ; return   =>   goto f
/// The function this is in has nothing left to do, so f may return straight
/// to its caller. That saves a level of the hardware stack as well.
bool PIC16F1Peephole::tailCalls(MachineFunction &MF) {
  const PIC16F1InstrInfo &TII =
      *MF.getSubtarget<PIC16F1Subtarget>().getInstrInfo();
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    MachineBasicBlock::iterator Ret = MBB.getLastNonDebugInstr();
    if (Ret == MBB.end() || Ret->getOpcode() != PIC16F1::RETURN ||
        Ret == MBB.begin() || isConditional(*Ret))
      continue;
    MachineBasicBlock::iterator Call = std::prev(Ret);
    while (Call != MBB.begin() && !isReal(*Call))
      --Call;
    if (Call->getOpcode() != PIC16F1::CALL || isConditional(*Call))
      continue;
    MachineInstrBuilder Jump =
        BuildMI(MBB, Call, Call->getDebugLoc(), TII.get(PIC16F1::GOTO));
    // The arguments stay listed as read; the results are no longer this
    // function's business.
    for (const MachineOperand &MO : Call->operands())
      if (!MO.isReg() || MO.isUse())
        Jump.add(MO);
    Call->eraseFromParent();
    Ret->eraseFromParent();
    Changed = true;
  }
  return Changed;
}

/// True if \p MI reads or changes FSR \p N, or may: by name, as a file
/// register, or through INDF.
static bool touchesFSR(const MachineInstr &MI, unsigned N) {
  using namespace PIC16F1;
  switch (MI.getOpcode()) {
  case ADDFSR:
    return MI.getOperand(0).getImm() == N;
  case MOVIWm:
  case MOVWIm:
    return ((MI.getOperand(0).getImm() >> 2) & 1) == N;
  case MOVIWk:
  case MOVWIk:
    return MI.getOperand(1).getImm() == N;
  default:
    break;
  }
  if (MI.isCall() || MI.isReturn() || MI.isBranch() || MI.isInlineAsm())
    return true;
  // FSRnL, FSRnH and INDFn as file operands; anything else that is not a
  // plain number (a symbol) is some other variable.
  const unsigned Lo = N ? FSR1L_ADDR : FSR0L_ADDR;
  for (const MachineOperand &MO : MI.explicit_operands())
    if (MO.isImm()) {
      unsigned F = MO.getImm() & 0x7F;
      if (F == Lo || F == Lo + 1 || F == N)
        return true;
    }
  return false;
}

/// An access through an FSR and a step of that FSR by one, with nothing
/// between them that uses it, are one instruction: MOVIW FSR0++ and its
/// relatives. The step moves to the access, which is safe when no branch
/// lies between the two.
bool PIC16F1Peephole::stepPointer(MachineBasicBlock &MBB) {
  using namespace PIC16F1;
  const PIC16F1InstrInfo &TII =
      *MBB.getParent()->getSubtarget<PIC16F1Subtarget>().getInstrInfo();
  bool Changed = false;
  for (unsigned N = 0; N != 2; ++N) {
    auto IsAccess = [N](const MachineInstr &MI) {
      return (MI.getOpcode() == MOVIWk || MI.getOpcode() == MOVWIk) &&
             MI.getOperand(0).getImm() == 0 && MI.getOperand(1).getImm() == N;
    };
    auto IsStep = [N](const MachineInstr &MI) {
      return MI.getOpcode() == ADDFSR && MI.getOperand(0).getImm() == N &&
             (MI.getOperand(1).getImm() == 1 ||
              MI.getOperand(1).getImm() == -1) &&
             !MI.getFlag(MachineInstr::FrameSetup) &&
             !MI.getFlag(MachineInstr::FrameDestroy);
    };
    for (auto I = MBB.begin(); I != MBB.end(); ++I) {
      if (!isReal(*I) || !(IsAccess(*I) || IsStep(*I)) || isConditional(*I))
        continue;
      bool First = IsAccess(*I);
      for (auto J = std::next(I); J != MBB.end(); ++J) {
        if (!isReal(*J))
          continue;
        if ((First ? IsStep(*J) : IsAccess(*J)) && !isConditional(*J)) {
          MachineInstr &Access = First ? *I : *J;
          MachineInstr &Step = First ? *J : *I;
          bool Up = Step.getOperand(1).getImm() == 1;
          // Mode: 0 ++FSR, 1 --FSR, 2 FSR++, 3 FSR--; bit 2 is the FSR.
          unsigned Mode = (First ? 2 : 0) | (Up ? 0 : 1) | (N << 2);
          Access.setDesc(
              TII.get(Access.getOpcode() == MOVIWk ? MOVIWm : MOVWIm));
          Access.removeOperand(1);
          Access.getOperand(0).setImm(Mode);
          if (First) {
            J->eraseFromParent();
          } else {
            MBB.erase(I);
            I = J;
          }
          Changed = true;
          break;
        }
        if (touchesFSR(*J, N))
          break;
        // The file operands of the model are numbers; an instruction the
        // model does not understand at all stops the search.
        if (effectOf(*J).Barrier)
          break;
      }
    }
  }
  return Changed;
}

bool PIC16F1Peephole::runOnMachineFunction(MachineFunction &MF) {
  if (!EnablePeephole || MF.getTarget().getOptLevel() == CodeGenOptLevel::None ||
      skipFunction(MF.getFunction()))
    return false;
  bool Changed = false;
  // Each kind of change opens the way for the other; a few rounds settle it.
  for (unsigned Round = 0; Round != 8; ++Round) {
    bool RoundChanged = false;
    computeLiveness(MF);
    for (MachineBasicBlock &MBB : MF)
      RoundChanged |= removeDead(MBB);
    // Removing instructions only shrinks what is read; the liveness kept
    // per instruction stays a safe overestimate for the forward step.
    for (MachineBasicBlock &MBB : MF)
      RoundChanged |= forward(MBB);
    if (!RoundChanged)
      break;
    Changed = true;
  }
  if (Steps & 2048)
    for (MachineBasicBlock &MBB : MF)
      Changed |= stepPointer(MBB);
  if (Steps & 64)
    Changed |= removeBankSelects(MF);
  if (Steps & 128)
    Changed |= tailCalls(MF);
  return Changed;
}
