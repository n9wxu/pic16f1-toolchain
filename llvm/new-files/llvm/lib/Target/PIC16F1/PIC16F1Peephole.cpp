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
//
// Only W, the Z and C flags and the sixteen bytes of common RAM are
// modelled. Any other file register is memory, possibly a device register:
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
             "instructions, 32 fold bit tests"));

namespace {

// The modelled state, as bits: the sixteen bytes of common RAM, then W and
// the two flags.
enum : uint32_t {
  FilesMask = 0xFFFF,
  WBit = 1u << 16,
  ZBit = 1u << 17,
  CBit = 1u << 18,
  AllBits = FilesMask | WBit | ZBit | CBit,
};

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
  /// A common-RAM byte the instruction names, or -1.
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
  if (F >= 0x70)
    return 1u << (F - 0x70);
  // PCL (a write jumps), STATUS, and WREG (W itself).
  if (F == PIC16F1::PCL_ADDR || F == PIC16F1::STATUS_ADDR || F == 0x09)
    return ~0u;
  return 0;
}

/// The common-RAM bytes that make up a register, as bits.
uint32_t registerFiles(const MachineInstr &MI, MCRegister Reg) {
  const MachineFunction &MF = *MI.getMF();
  const auto &TII = *MF.getSubtarget<PIC16F1Subtarget>().getInstrInfo();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  uint32_t Bits = 0;
  for (MCPhysReg Sub : TRI.subregs_inclusive(Reg)) {
    if (!PIC16F1::GPR8RegClass.contains(Sub) && Sub != PIC16F1::TMP0 &&
        Sub != PIC16F1::TMP1)
      continue;
    unsigned F = TII.fileAddr(Sub);
    if (F >= 0x70 && F <= 0x7F)
      Bits |= 1u << (F - 0x70);
  }
  return Bits;
}

/// The common-RAM bytes a call may change, by its register mask.
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
      E.File = MO.getImm() & 0x7F;
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
  // Bank, page and pointer registers are outside the model.
  case MOVLB:
  case MOVLB6:
  case MOVLP:
  case ADDFSR:
    E.Memory = true;
    return E;
  case MOVIWm:
  case MOVIWk:
    E.Memory = true;
    E.Writes = WBit | ZBit;
    E.ResultInW = true;
    return E;
  case MOVWIm:
  case MOVWIk:
    E.Memory = true;
    E.Reads = WBit;
    return E;
  case BRA:
  case GOTO:
    return E;
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
    // Everything belongs to the interrupted code.
    E.Reads = FilesMask;
    E.Memory = true;
    return E;
  default:
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
        !(Live & ((1u << (E.File - 0x70)) | WBit | ZBit))) {
      MachineInstr *Store = nullptr, *Load = nullptr;
      if (It != MBB.rend() && It->getOpcode() == MOVWF &&
          It->getOperand(0).isImm() &&
          (It->getOperand(0).getImm() & 0x7F) == E.File)
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
  int CopyOf[16];         // the register each byte register is a copy of
  for (int I = 0; I != 16; ++I)
    CopyOf[I] = I;

  auto ForgetW = [&] {
    WConst = -1;
    WEq = 0;
    ZIsW = false;
  };
  auto ForgetFile = [&](int Idx) {
    WEq &= ~(1u << Idx);
    if (ZFile == Idx)
      ZFile = -1;
    // Its copies are still copies of one another, but that is not kept.
    for (int I = 0; I != 16; ++I)
      if (CopyOf[I] == Idx)
        CopyOf[I] = I;
    CopyOf[Idx] = Idx;
  };
  auto ForgetAll = [&] {
    ForgetW();
    ZFile = -1;
    for (int I = 0; I != 16; ++I)
      CopyOf[I] = I;
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
    int Idx = E.File >= 0 ? E.File - 0x70 : -1;
    uint32_t After = LiveAfter.lookup(&MI);

    // Read the original instead of a copy.
    if ((Steps & 1) && Idx >= 0 && (E.Reads & (1u << Idx)) &&
        !(E.Writes & (1u << Idx)) && CopyOf[Idx] != Idx) {
      Idx = CopyOf[Idx];
      MI.getOperand(0).setImm(0x70 + Idx);
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
      MI.getOperand(0).setImm(0x70 + CopyOf[Idx]);
      MI.getOperand(1).setImm(0);
      MachineInstr *Store =
          BuildMI(MBB, It, MI.getDebugLoc(), TII.get(MOVWF)).addImm(0x70 + Idx);
      // Until the next round, treat what follows the store as following
      // the operation.
      LiveAfter[Store] = After;
      ForgetFile(Idx);
      WConst = -1;
      WEq = 1u << Idx;
      ZIsW = (E.Writes & ZBit) != 0;
      ZFile = ZIsW ? Idx : (E.Writes & ZBit) ? -1 : ZFile;
      It = std::next(Store->getIterator());
      Changed = true;
      continue;
    }

    // Update what is known.
    if (Cond) {
      if (E.Writes & WBit)
        ForgetW();
      if (E.Writes & ZBit) {
        ZIsW = false;
        ZFile = -1;
      }
      if (Idx >= 0 && (E.Writes & (1u << Idx)))
        ForgetFile(Idx);
      continue;
    }
    if (Opc == CALL) {
      ForgetAll();
      continue;
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
          for (int I = 0; I != 16; ++I)
            if (CopyOf[I] == Root)
              WEq |= 1u << I;
        }
        if (Opc == CLRW)
          WConst = 0;
        ZIsW = (E.Writes & ZBit) != 0;
        if (E.Writes & ZBit)
          ZFile = Opc == MOVF && Idx >= 0 ? Idx : -1;
      }
      continue;
    }
    if (E.Writes & ZBit) {
      ZIsW = false;
      ZFile = -1;
    }
    if (Idx >= 0 && (E.Writes & (1u << Idx))) {
      if (Opc == MOVWF) {
        // The register is now a copy of whatever W was loaded from.
        int Root = -1;
        for (int I = 0; I != 16 && Root < 0; ++I)
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
  return Changed;
}
