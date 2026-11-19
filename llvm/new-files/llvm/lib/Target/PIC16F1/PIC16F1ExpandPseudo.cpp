//===-- PIC16F1ExpandPseudo.cpp - Expand pseudo instructions --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Two passes turn code-generator pseudo instructions into machine
// instructions:
//
//  * PIC16F1ExpandPseudo handles everything except branches. While it emits
//    code it remembers, within a basic block, what W and BSR hold and which
//    pair FSR0 was last copied from, and leaves out instructions that would
//    reload a value already there.
//
//  * PIC16F1ExpandBranches handles the branch pseudos. It runs after branch
//    relaxation, which needs each branch to still be one instruction.
//
// Inside an expansion W, BSR, the Z and DC flags and (where the pseudo says
// so) TMP0 are scratch. FSR0 is not: it is a register like any other, and a
// load or store through a pointer finds the pointer in it already. The carry flag is preserved by every expansion
// that does not list it as defined, because wide add and subtract chains keep
// it live from one pseudo to the next.
//
//===----------------------------------------------------------------------===//

#include "PIC16F1.h"
#include "PIC16F1InstrInfo.h"
#include "PIC16F1Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "pic16f1-expand-pseudo"

static cl::opt<bool> TrackValues(
    "pic16f1-track-values", cl::Hidden, cl::init(true),
    cl::desc("Skip reloads of values W, BSR and FSR0 already hold"));

namespace {

/// Emits machine instructions before a given point and tracks register
/// contents within the block.
class Emitter {
  MachineBasicBlock *MBB = nullptr;
  MachineBasicBlock::iterator I;
  DebugLoc DL;
  const PIC16F1InstrInfo &TII;

  // W equals the contents of this unbanked file register, or -1.
  int WFile = -1;
  // W equals this constant, or -1.
  int WConst = -1;
  // BSR holds this bank number, or -1.
  int BSRConst = -1;
  // BSR holds the bank of this symbol plus offset.
  const GlobalValue *BSRGlobal = nullptr;
  int64_t BSROffset = 0;

public:
  // FSR0 equals this register pair, or no register; and the same for FSR1
  // in a function where it is a register.
  Register FSR0Holds;
  Register FSR1Holds;

  // The device has 64 banks.
  bool Banks64 = false;

  explicit Emitter(const PIC16F1InstrInfo &TII) : TII(TII) {}

  unsigned movlb() const { return Banks64 ? PIC16F1::MOVLB6 : PIC16F1::MOVLB; }

  void setInsertPoint(MachineBasicBlock &B, MachineBasicBlock::iterator It) {
    MBB = &B;
    I = It;
    DL = It != B.end() ? It->getDebugLoc() : DebugLoc();
  }

  void forgetW() { WFile = WConst = -1; }
  void forgetBSR() {
    BSRConst = -1;
    BSRGlobal = nullptr;
  }
  void forgetAll() {
    forgetW();
    forgetBSR();
    FSR0Holds = Register();
    FSR1Holds = Register();
  }

  /// Core registers and common RAM are the same byte in every bank.
  static bool isUnbanked(unsigned F) { return F < 0x0C || F >= 0x70; }

  void wroteFile(unsigned F) {
    if (WFile == int(F))
      WFile = -1;
  }

  // --- Raw instruction builders -------------------------------------------

  MachineInstrBuilder build(unsigned Opc) {
    return BuildMI(*MBB, I, DL, TII.get(Opc));
  }

  /// W = file register.
  void loadW(unsigned F) {
    if (TrackValues && WFile == int(F))
      return;
    build(PIC16F1::MOVF).addImm(F).addImm(0);
    WFile = F;
    WConst = -1;
  }
  /// W = constant.
  void loadWImm(unsigned K) {
    K &= 0xFF;
    if (TrackValues && WConst == int(K))
      return;
    build(PIC16F1::MOVLW).addImm(K);
    WConst = K;
    WFile = -1;
  }
  /// W = part of a symbol's address.
  void loadWSym(const MachineOperand &Sym, unsigned Flags) {
    MachineOperand MO = Sym;
    MO.setTargetFlags(Flags);
    build(PIC16F1::MOVLW).add(MO);
    forgetW();
  }
  /// file register = W.
  void storeW(unsigned F) {
    build(PIC16F1::MOVWF).addImm(F);
    // W and the file register now agree. An earlier constant stays known.
    WFile = F;
  }
  void clear(unsigned F) {
    build(PIC16F1::CLRF).addImm(F);
    wroteFile(F);
  }
  /// file = file OP W (or a unary operation on the file register).
  void opToFile(unsigned Opc, unsigned F) {
    build(Opc).addImm(F).addImm(1);
    wroteFile(F);
  }
  /// W = file OP W.
  void opToW(unsigned Opc, unsigned F) {
    build(Opc).addImm(F).addImm(0);
    forgetW();
  }
  void opLit(unsigned Opc, unsigned K) {
    build(Opc).addImm(K & 0xFF);
    forgetW();
  }
  void bitOp(unsigned Opc, unsigned F, unsigned Bit) {
    build(Opc).addImm(F).addImm(Bit);
    if (Opc == PIC16F1::BCF || Opc == PIC16F1::BSF)
      wroteFile(F);
  }
  void addFSR(unsigned N, int64_t K) {
    build(PIC16F1::ADDFSR).addImm(N).addImm(K);
  }
  /// W = *(FSRn + K)
  void loadIndirect(unsigned N, int64_t K) {
    build(PIC16F1::MOVIWk).addImm(K).addImm(N);
    forgetW();
  }
  /// *(FSRn + K) = W
  void storeIndirect(unsigned N, int64_t K) {
    build(PIC16F1::MOVWIk).addImm(K).addImm(N);
  }

  // --- Copies between byte registers ---------------------------------------

  void copy(unsigned Dst, unsigned Src) {
    if (Dst == Src)
      return;
    loadW(Src);
    storeW(Dst);
  }
  void setImm(unsigned Dst, unsigned K) {
    K &= 0xFF;
    if (K == 0 && WConst != 0) {
      clear(Dst);
      return;
    }
    loadWImm(K);
    storeW(Dst);
  }

  // --- Banked (direct) data access -----------------------------------------

  static bool inOneBank(const GlobalValue *GV, int64_t Off) {
    return PIC16F1InstrInfo::inOneBank(GV, Off);
  }

  /// Select the bank of the address in \p Addr plus \p Extra and return the
  /// file-register operand for it.
  MachineOperand selectBank(const MachineOperand &Addr, int64_t Extra) {
    if (Addr.isImm()) {
      uint64_t A = uint64_t(Addr.getImm() + Extra) & (Banks64 ? 0x1FFF : 0xFFF);
      unsigned F = A & 0x7F;
      int Bank = A >> 7;
      if (!isUnbanked(F) && !(TrackValues && BSRConst == Bank)) {
        build(movlb()).addImm(Bank);
        BSRConst = Bank;
        BSRGlobal = nullptr;
      }
      return MachineOperand::CreateImm(F);
    }
    assert(Addr.isGlobal() && "direct address must be constant or global");
    const GlobalValue *GV = Addr.getGlobal();
    int64_t Off = Addr.getOffset() + Extra;
    if (!(TrackValues && BSRGlobal == GV &&
          (BSROffset == Off || (inOneBank(GV, Off) && inOneBank(GV, BSROffset))))) {
      build(movlb()).add(MachineOperand::CreateGA(GV, Off, PIC16F1II::MO_BANK));
      BSRGlobal = GV;
      BSROffset = Off;
      BSRConst = -1;
    }
    return MachineOperand::CreateGA(GV, Off, PIC16F1II::MO_NONE);
  }

  /// Dst = byte at direct address.
  void loadDirect(unsigned Dst, const MachineOperand &Addr, int64_t Extra) {
    MachineOperand F = selectBank(Addr, Extra);
    build(PIC16F1::MOVF).add(F).addImm(0);
    forgetW();
    storeW(Dst);
  }
  /// Byte at direct address = Src.
  void storeDirect(unsigned Src, const MachineOperand &Addr, int64_t Extra) {
    loadW(Src);
    MachineOperand F = selectBank(Addr, Extra);
    build(PIC16F1::MOVWF).add(F);
    // The target may be a core register that mirrors W or holds state we
    // track, when it is a constant address in the core-register range.
    if (F.isImm() && isUnbanked(F.getImm()))
      forgetAll();
  }

  // --- The stack pointer ----------------------------------------------------

  /// The function has a stack frame and so needs its stack pointer in FSR1.
  bool UsesStackPointer = false;

  /// After a call FSR1 may hold anything: a function with a static frame
  /// uses it as a register. A function with a stack frame gets its stack
  /// pointer back from the cell, where it put it before its first call, but
  /// not until it needs it: calls that follow one another need nothing in
  /// between.
  bool StackPointerStale = false;
  void reloadStackPointer(const MachineInstr &Call) {
    if (UsesStackPointer)
      StackPointerStale = true;
  }
  /// Put the stack pointer back in FSR1 if a call has been made since it
  /// last was.
  void needStackPointer(const MachineFunction &MF) {
    if (!StackPointerStale)
      return;
    StackPointerStale = false;
    MachineOperand Cell = MachineOperand::CreateGA(
        pic16f1StackPointerCell(*MF.getFunction().getParent()), 0);
    loadDirect(PIC16F1::FSR1L_ADDR, Cell, 0);
    loadDirect(PIC16F1::FSR1H_ADDR, Cell, 1);
  }

  // --- Pointer access through FSR0 -----------------------------------------

  /// The pointer-setup pass put every pointer in FSR0 or FSR1 already;
  /// this gives the number of the one \p Pair is.
  unsigned pointFSR0At(Register Pair, unsigned PairAddr) {
    if (Pair == PIC16F1::FSR0)
      return 0;
    if (Pair == PIC16F1::SP)
      return 1;
    report_fatal_error("PIC16F1: pointer operand is not in FSR0 or FSR1");
  }

  // --- Stack access through FSR1 -------------------------------------------

  /// Run \p Body with FSR1 moved so that stack offset \p Off (and the
  /// \p Bytes - 1 after it) is within reach; \p Body gets the adjusted
  /// offset of the first byte.
  template <typename Fn> void withStackSlot(int64_t Off, unsigned Bytes, Fn Body) {
    int64_t Limit = 31 - (Bytes - 1);
    int64_t Moved = 0;
    while (Off - Moved > Limit) {
      addFSR(1, 31);
      Moved += 31;
    }
    while (Off - Moved < -32) {
      addFSR(1, -32);
      Moved -= 32;
    }
    Body(Off - Moved);
    while (Moved > 0) {
      addFSR(1, -31);
      Moved -= 31;
    }
    while (Moved < 0) {
      addFSR(1, 32);
      Moved += 32;
    }
  }
};

class PIC16F1ExpandPseudo : public MachineFunctionPass {
public:
  static char ID;
  PIC16F1ExpandPseudo() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override {
    return "PIC16F1 pseudo instruction expansion";
  }

private:
  const PIC16F1InstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;

  /// Returns true if \p MI was expanded and must be erased.
  bool expand(MachineInstr &MI, Emitter &E);

  unsigned lo(const MachineOperand &MO) const {
    return TII->fileAddr(MO.getReg());
  }
  unsigned hi(const MachineOperand &MO) const {
    return TII->fileAddr(MO.getReg()) + 1;
  }
};

} // end anonymous namespace

char PIC16F1ExpandPseudo::ID = 0;

INITIALIZE_PASS(PIC16F1ExpandPseudo, DEBUG_TYPE,
                "PIC16F1 pseudo instruction expansion", false, false)

FunctionPass *llvm::createPIC16F1ExpandPseudoPass() {
  return new PIC16F1ExpandPseudo();
}

/// Position of the single set bit, or -1.
static int singleBit(unsigned V) {
  V &= 0xFF;
  return V && !(V & (V - 1)) ? int(Log2_32(V)) : -1;
}

bool PIC16F1ExpandPseudo::expand(MachineInstr &MI, Emitter &E) {
  using namespace PIC16F1;
  const unsigned Opc = MI.getOpcode();
  const bool CarryDead = MI.registerDefIsDead(CARRY, TRI);

  // Byte-wise logic with a constant.
  auto LogicImm = [&](unsigned Op, unsigned F, unsigned K) {
    K &= 0xFF;
    switch (Op) {
    case ANDWF:
      if (K == 0xFF)
        return;
      if (K == 0)
        return E.clear(F);
      if (int B = singleBit(~K); B >= 0)
        return E.bitOp(BCF, F, B);
      break;
    case IORWF:
      if (K == 0)
        return;
      if (int B = singleBit(K); B >= 0)
        return E.bitOp(BSF, F, B);
      break;
    case XORWF:
      if (K == 0)
        return;
      if (K == 0xFF)
        return E.opToFile(COMF, F);
      break;
    }
    E.loadWImm(K);
    E.opToFile(Op, F);
  };

  switch (Opc) {
  default:
    return false;

  case MOV8rr:
    E.copy(lo(MI.getOperand(0)), lo(MI.getOperand(1)));
    return true;
  case MOV16rr:
    // FSR0 is often pointed at the same pair again and again.
    if (MI.getOperand(0).getReg() == PIC16F1::FSR0 && TrackValues &&
        E.FSR0Holds == MI.getOperand(1).getReg())
      return true;
    if (MI.getOperand(0).getReg() == PIC16F1::SP && TrackValues &&
        E.FSR1Holds == MI.getOperand(1).getReg())
      return true;
    E.copy(lo(MI.getOperand(0)), lo(MI.getOperand(1)));
    E.copy(hi(MI.getOperand(0)), hi(MI.getOperand(1)));
    return true;

  case LDI8:
    E.setImm(lo(MI.getOperand(0)), MI.getOperand(1).getImm());
    return true;
  case LDI16: {
    const MachineOperand &Src = MI.getOperand(1);
    if (Src.isImm()) {
      E.setImm(lo(MI.getOperand(0)), Src.getImm());
      E.setImm(hi(MI.getOperand(0)), Src.getImm() >> 8);
    } else {
      bool Code = Src.getTargetFlags() == PIC16F1II::MO_PM;
      E.loadWSym(Src, Code ? PIC16F1II::MO_PM_LO8 : PIC16F1II::MO_LO8);
      E.storeW(lo(MI.getOperand(0)));
      E.loadWSym(Src, Code ? PIC16F1II::MO_PM_HI8 : PIC16F1II::MO_HI8);
      E.storeW(hi(MI.getOperand(0)));
    }
    return true;
  }

  // --- 8-bit arithmetic ----------------------------------------------------
  case ADD8rr:
  case SUB8rr:
  case AND8rr:
  case OR8rr:
  case XOR8rr: {
    unsigned Op = Opc == ADD8rr   ? ADDWF
                  : Opc == SUB8rr ? SUBWF
                  : Opc == AND8rr ? ANDWF
                  : Opc == OR8rr  ? IORWF
                                  : XORWF;
    E.loadW(lo(MI.getOperand(2)));
    E.opToFile(Op, lo(MI.getOperand(0)));
    return true;
  }
  case ADD8ri: {
    unsigned D = lo(MI.getOperand(0));
    unsigned K = MI.getOperand(2).getImm() & 0xFF;
    // INCF and DECF do not produce a carry.
    if (CarryDead && K == 1)
      E.opToFile(INCF, D);
    else if (CarryDead && K == 0xFF)
      E.opToFile(DECF, D);
    else {
      E.loadWImm(K);
      E.opToFile(ADDWF, D);
    }
    return true;
  }
  case AND8ri:
    LogicImm(ANDWF, lo(MI.getOperand(0)), MI.getOperand(2).getImm());
    return true;
  case OR8ri:
    LogicImm(IORWF, lo(MI.getOperand(0)), MI.getOperand(2).getImm());
    return true;
  case XOR8ri:
    LogicImm(XORWF, lo(MI.getOperand(0)), MI.getOperand(2).getImm());
    return true;
  case NOT8:
    E.opToFile(COMF, lo(MI.getOperand(0)));
    return true;
  case SWAP8:
    E.opToFile(SWAPF, lo(MI.getOperand(0)));
    return true;

  // Add a constant if the value is negative. W gets the constant only if
  // the sign bit is set, and zero otherwise.
  case ADDNEG8:
    E.loadWImm(MI.getOperand(2).getImm());
    E.bitOp(BTFSC, lo(MI.getOperand(0)), 7);
    E.opToFile(ADDWF, lo(MI.getOperand(0)));
    return true;
  case ADDNEG16: {
    uint64_t K = MI.getOperand(2).getImm();
    for (unsigned B = 0; B != 2; ++B) {
      unsigned Byte = (K >> (8 * B)) & 0xFF;
      E.loadWImm(0);
      if (Byte != 0) {
        E.bitOp(BTFSC, hi(MI.getOperand(0)), 7);
        E.build(MOVLW).addImm(Byte);
        E.forgetW();
      }
      E.opToFile(B == 0 ? ADDWF : ADDWFC, B == 0 ? lo(MI.getOperand(0))
                                                 : hi(MI.getOperand(0)));
    }
    return true;
  }
  case NEG8:
    E.opToFile(COMF, lo(MI.getOperand(0)));
    E.opToFile(INCF, lo(MI.getOperand(0)));
    return true;
  case SHL8:
    E.opToFile(LSLF, lo(MI.getOperand(0)));
    return true;
  case LSR8:
    E.opToFile(LSRF, lo(MI.getOperand(0)));
    return true;
  case ASR8:
    E.opToFile(ASRF, lo(MI.getOperand(0)));
    return true;

  // --- 16-bit arithmetic ---------------------------------------------------
  case ADD16rr:
  case SUB16rr:
  case ADC16rr:
  case SBC16rr: {
    bool Add = Opc == ADD16rr || Opc == ADC16rr;
    bool CarryIn = Opc == ADC16rr || Opc == SBC16rr;
    unsigned First = Add ? (CarryIn ? ADDWFC : ADDWF)
                         : (CarryIn ? SUBWFB : SUBWF);
    E.loadW(lo(MI.getOperand(2)));
    E.opToFile(First, lo(MI.getOperand(0)));
    E.loadW(hi(MI.getOperand(2)));
    E.opToFile(Add ? ADDWFC : SUBWFB, hi(MI.getOperand(0)));
    return true;
  }
  case ADD16ri: {
    if (!MI.getOperand(2).isImm()) {
      // The address of a data symbol.
      E.loadWSym(MI.getOperand(2), PIC16F1II::MO_LO8);
      E.opToFile(ADDWF, lo(MI.getOperand(0)));
      E.loadWSym(MI.getOperand(2), PIC16F1II::MO_HI8);
      E.opToFile(ADDWFC, hi(MI.getOperand(0)));
      return true;
    }
    uint64_t K = MI.getOperand(2).getImm();
    if (int64_t S = int16_t(K);
        CarryDead && S >= -32 && S <= 31 &&
        (MI.getOperand(0).getReg() == PIC16F1::FSR0 ||
         MI.getOperand(0).getReg() == PIC16F1::SP)) {
      if (S != 0)
        E.addFSR(MI.getOperand(0).getReg() == PIC16F1::SP, S);
      return true;
    }
    if (CarryDead && (K & 0xFF) == 0) {
      // Only the high byte changes.
      unsigned KHi = (K >> 8) & 0xFF;
      if (KHi == 1)
        E.opToFile(INCF, hi(MI.getOperand(0)));
      else if (KHi == 0xFF)
        E.opToFile(DECF, hi(MI.getOperand(0)));
      else if (KHi != 0) {
        E.loadWImm(KHi);
        E.opToFile(ADDWF, hi(MI.getOperand(0)));
      }
      return true;
    }
    if (CarryDead && (K & 0xFFFF) == 1) {
      // Increment: the high byte follows when the low byte wraps to zero.
      E.opToFile(INCF, lo(MI.getOperand(0)));
      E.bitOp(BTFSC, STATUS_ADDR, STATUS_Z);
      E.opToFile(INCF, hi(MI.getOperand(0)));
      return true;
    }
    E.loadWImm(K);
    E.opToFile(ADDWF, lo(MI.getOperand(0)));
    E.loadWImm(K >> 8);
    E.opToFile(ADDWFC, hi(MI.getOperand(0)));
    return true;
  }
  case AND16rr:
  case OR16rr:
  case XOR16rr: {
    unsigned Op = Opc == AND16rr ? ANDWF : Opc == OR16rr ? IORWF : XORWF;
    E.loadW(lo(MI.getOperand(2)));
    E.opToFile(Op, lo(MI.getOperand(0)));
    E.loadW(hi(MI.getOperand(2)));
    E.opToFile(Op, hi(MI.getOperand(0)));
    return true;
  }
  case AND16ri:
  case OR16ri:
  case XOR16ri: {
    unsigned Op = Opc == AND16ri ? ANDWF : Opc == OR16ri ? IORWF : XORWF;
    uint64_t K = MI.getOperand(2).getImm();
    LogicImm(Op, lo(MI.getOperand(0)), K);
    LogicImm(Op, hi(MI.getOperand(0)), K >> 8);
    return true;
  }
  case NOT16:
    E.opToFile(COMF, lo(MI.getOperand(0)));
    E.opToFile(COMF, hi(MI.getOperand(0)));
    return true;
  case NEG16:
    // -x = ~x + 1; the increment carries into the high byte when the low
    // byte wraps to zero.
    E.opToFile(COMF, lo(MI.getOperand(0)));
    E.opToFile(COMF, hi(MI.getOperand(0)));
    E.opToFile(INCF, lo(MI.getOperand(0)));
    E.bitOp(BTFSC, STATUS_ADDR, STATUS_Z);
    E.opToFile(INCF, hi(MI.getOperand(0)));
    return true;
  case SHL16:
    E.opToFile(LSLF, lo(MI.getOperand(0)));
    E.opToFile(RLF, hi(MI.getOperand(0)));
    return true;
  case LSR16:
    E.opToFile(LSRF, hi(MI.getOperand(0)));
    E.opToFile(RRF, lo(MI.getOperand(0)));
    return true;
  case ASR16:
    E.opToFile(ASRF, hi(MI.getOperand(0)));
    E.opToFile(RRF, lo(MI.getOperand(0)));
    return true;

  case ROL16:
    E.opToFile(RLF, lo(MI.getOperand(0)));
    E.opToFile(RLF, hi(MI.getOperand(0)));
    return true;
  case ROR16:
    E.opToFile(RRF, hi(MI.getOperand(0)));
    E.opToFile(RRF, lo(MI.getOperand(0)));
    return true;

  case CARRY8:
    // RLF moves the carry into bit 0 of the cleared byte.
    E.clear(lo(MI.getOperand(0)));
    E.opToFile(RLF, lo(MI.getOperand(0)));
    return true;
  case BORROW8:
    // A borrow leaves the carry clear. CLRF and INCF do not change it.
    E.clear(lo(MI.getOperand(0)));
    E.bitOp(BTFSS, STATUS_ADDR, STATUS_C);
    E.opToFile(INCF, lo(MI.getOperand(0)));
    return true;

  case ZEXT16:
    E.copy(lo(MI.getOperand(0)), lo(MI.getOperand(1)));
    E.clear(hi(MI.getOperand(0)));
    return true;
  case SEXT16:
    E.copy(lo(MI.getOperand(0)), lo(MI.getOperand(1)));
    E.clear(hi(MI.getOperand(0)));
    E.bitOp(BTFSC, lo(MI.getOperand(0)), 7);
    E.opToFile(DECF, hi(MI.getOperand(0)));
    return true;

  // --- Read-modify-write of a directly addressed byte ----------------------
  case AND8mi:
  case OR8mi:
  case XOR8mi:
  case ADD8mi:
  case AND8mr:
  case OR8mr:
  case XOR8mr:
  case ADD8mr: {
    bool IsImm = Opc == AND8mi || Opc == OR8mi || Opc == XOR8mi || Opc == ADD8mi;
    unsigned Op = (Opc == AND8mi || Opc == AND8mr)   ? ANDWF
                  : (Opc == OR8mi || Opc == OR8mr)   ? IORWF
                  : (Opc == XOR8mi || Opc == XOR8mr) ? XORWF
                                                     : ADDWF;
    // Pick a single instruction where one exists; otherwise W holds the
    // other operand. W must be loaded before the bank is selected only in
    // the sense that neither step disturbs the other.
    unsigned Single = 0;
    int Bit = -1;
    if (IsImm) {
      unsigned K = MI.getOperand(1).getImm() & 0xFF;
      if (Op == ANDWF && singleBit(~K) >= 0) {
        Single = BCF;
        Bit = singleBit(~K);
      } else if (Op == IORWF && singleBit(K) >= 0) {
        Single = BSF;
        Bit = singleBit(K);
      } else if (Op == XORWF && K == 0xFF) {
        Single = COMF;
      } else if (Op == ADDWF && K == 1 && CarryDead) {
        Single = INCF;
      } else if (Op == ADDWF && K == 0xFF && CarryDead) {
        Single = DECF;
      } else if (Op == ANDWF && K == 0) {
        Single = CLRF;
      } else {
        E.loadWImm(K);
      }
    } else {
      E.loadW(lo(MI.getOperand(1)));
    }
    MachineOperand F = E.selectBank(MI.getOperand(0), 0);
    if (Single == BCF || Single == BSF)
      E.build(Single).add(F).addImm(Bit);
    else if (Single == CLRF)
      E.build(CLRF).add(F);
    else
      E.build(Single ? Single : Op).add(F).addImm(1);
    // A constant address may name a core register this pass tracks.
    if (F.isImm() && Emitter::isUnbanked(F.getImm()))
      E.forgetAll();
    return true;
  }

  case SUB8mr: {
    E.loadW(lo(MI.getOperand(1)));
    MachineOperand F = E.selectBank(MI.getOperand(0), 0);
    E.build(SUBWF).add(F).addImm(1);
    if (F.isImm() && Emitter::isUnbanked(F.getImm()))
      E.forgetAll();
    return true;
  }

  // --- Read-modify-write of a directly addressed 16-bit object -------------
  case AND16mi:
  case OR16mi:
  case XOR16mi:
  case ADD16mi:
  case AND16mr:
  case OR16mr:
  case XOR16mr:
  case ADD16mr:
  case SUB16mr: {
    bool IsImm = Opc == AND16mi || Opc == OR16mi || Opc == XOR16mi ||
                 Opc == ADD16mi;
    unsigned Op = (Opc == AND16mi || Opc == AND16mr)   ? ANDWF
                  : (Opc == OR16mi || Opc == OR16mr)   ? IORWF
                  : (Opc == XOR16mi || Opc == XOR16mr) ? XORWF
                  : Opc == SUB16mr                     ? SUBWF
                                                       : ADDWF;
    const MachineOperand &Addr = MI.getOperand(0);
    // The bank is selected before each instruction on the object; MOVLB
    // changes no flag, so it may stand between the two halves.
    auto OnByte = [&](unsigned B, unsigned Inst, int Bit = -1) {
      MachineOperand F = E.selectBank(Addr, B);
      if (Inst == BCF || Inst == BSF)
        E.build(Inst).add(F).addImm(Bit);
      else if (Inst == CLRF)
        E.build(CLRF).add(F);
      else
        E.build(Inst).add(F).addImm(1);
      if (F.isImm() && Emitter::isUnbanked(F.getImm()))
        E.forgetAll();
    };
    if (!IsImm) {
      E.loadW(lo(MI.getOperand(1)));
      OnByte(0, Op);
      E.loadW(hi(MI.getOperand(1)));
      OnByte(1, Op == ADDWF ? ADDWFC : Op == SUBWF ? SUBWFB : Op);
      return true;
    }
    uint64_t K = MI.getOperand(1).getImm() & 0xFFFF;
    if (Op == ADDWF) {
      unsigned KLo = K & 0xFF, KHi = K >> 8;
      if (CarryDead && K == 1) {
        // The high byte follows when the low byte wraps to zero. Its bank
        // is selected first: the skip must act on the INCF.
        OnByte(0, INCF);
        MachineOperand F = E.selectBank(Addr, 1);
        E.bitOp(BTFSC, STATUS_ADDR, STATUS_Z);
        E.build(INCF).add(F).addImm(1);
        if (F.isImm() && Emitter::isUnbanked(F.getImm()))
          E.forgetAll();
        return true;
      }
      if (CarryDead && KLo == 0) {
        if (KHi == 1)
          OnByte(1, INCF);
        else if (KHi == 0xFF)
          OnByte(1, DECF);
        else if (KHi != 0) {
          E.loadWImm(KHi);
          OnByte(1, ADDWF);
        }
        return true;
      }
      E.loadWImm(KLo);
      OnByte(0, ADDWF);
      E.loadWImm(KHi);
      OnByte(1, ADDWFC);
      return true;
    }
    for (unsigned B = 0; B != 2; ++B) {
      unsigned Byte = (K >> (8 * B)) & 0xFF;
      if (Op == ANDWF) {
        if (Byte == 0xFF)
          continue;
        if (Byte == 0) {
          OnByte(B, CLRF);
          continue;
        }
        if (int Bit = singleBit(~Byte); Bit >= 0) {
          OnByte(B, BCF, Bit);
          continue;
        }
      } else if (Op == IORWF) {
        if (Byte == 0)
          continue;
        if (int Bit = singleBit(Byte); Bit >= 0) {
          OnByte(B, BSF, Bit);
          continue;
        }
      } else {
        if (Byte == 0)
          continue;
        if (Byte == 0xFF) {
          OnByte(B, COMF);
          continue;
        }
      }
      E.loadWImm(Byte);
      OnByte(B, Op);
    }
    return true;
  }

  // --- Direct loads and stores ---------------------------------------------
  case LD8abs:
    E.loadDirect(lo(MI.getOperand(0)), MI.getOperand(1), 0);
    return true;
  case LD16abs:
    E.loadDirect(lo(MI.getOperand(0)), MI.getOperand(1), 0);
    E.loadDirect(hi(MI.getOperand(0)), MI.getOperand(1), 1);
    return true;
  case ST8abs:
    E.storeDirect(lo(MI.getOperand(0)), MI.getOperand(1), 0);
    return true;
  case ST16abs:
    E.storeDirect(lo(MI.getOperand(0)), MI.getOperand(1), 0);
    E.storeDirect(hi(MI.getOperand(0)), MI.getOperand(1), 1);
    return true;

  case STI8abs:
  case STI16abs: {
    // A constant to a direct address: CLRF for zero, else through W.
    uint64_t K = MI.getOperand(0).getImm();
    for (unsigned B = 0, N = Opc == STI16abs ? 2 : 1; B != N; ++B) {
      unsigned Byte = (K >> (8 * B)) & 0xFF;
      if (Byte != 0)
        E.loadWImm(Byte);
      MachineOperand F = E.selectBank(MI.getOperand(1), B);
      if (Byte == 0)
        E.build(CLRF).add(F);
      else
        E.build(MOVWF).add(F);
      if (F.isImm() && Emitter::isUnbanked(F.getImm()))
        E.forgetAll();
    }
    return true;
  }

  // --- Loads and stores through a pointer ----------------------------------
  case STI8ptr:
  case STI16ptr:
  case STI8ptr0:
  case STI16ptr0:
  case STI8ptr1:
  case STI16ptr1: {
    uint64_t K = MI.getOperand(0).getImm();
    int64_t Off = MI.getOperand(2).getImm();
    unsigned N = E.pointFSR0At(MI.getOperand(1).getReg(), lo(MI.getOperand(1)));
    E.loadWImm(K);
    E.storeIndirect(N, Off);
    if (Opc == STI16ptr || Opc == STI16ptr0 || Opc == STI16ptr1) {
      E.loadWImm(K >> 8);
      E.storeIndirect(N, Off + 1);
    }
    return true;
  }
  case LD8ptr:
  case LD16ptr:
  case LD8ptr0:
  case LD16ptr0:
  case LD8ptr1:
  case LD16ptr1: {
    int64_t Off = MI.getOperand(2).getImm();
    unsigned N = E.pointFSR0At(MI.getOperand(1).getReg(), lo(MI.getOperand(1)));
    E.loadIndirect(N, Off);
    E.storeW(lo(MI.getOperand(0)));
    if (Opc == LD16ptr || Opc == LD16ptr0 || Opc == LD16ptr1) {
      E.loadIndirect(N, Off + 1);
      E.storeW(hi(MI.getOperand(0)));
    }
    return true;
  }
  case ST8ptr:
  case ST16ptr:
  case ST8ptr0:
  case ST16ptr0:
  case ST8ptr1:
  case ST16ptr1: {
    int64_t Off = MI.getOperand(2).getImm();
    unsigned N = E.pointFSR0At(MI.getOperand(1).getReg(), lo(MI.getOperand(1)));
    E.loadW(lo(MI.getOperand(0)));
    E.storeIndirect(N, Off);
    if (Opc == ST16ptr || Opc == ST16ptr0 || Opc == ST16ptr1) {
      E.loadW(hi(MI.getOperand(0)));
      E.storeIndirect(N, Off + 1);
    }
    return true;
  }

  // --- Stack slots ---------------------------------------------------------
  case LD8fi:
  case LD16fi: {
    int64_t Off = MI.getOperand(1).getImm() + MI.getOperand(2).getImm();
    bool Wide = Opc == LD16fi;
    E.withStackSlot(Off, Wide ? 2 : 1, [&](int64_t K) {
      E.loadIndirect(1, K);
      E.storeW(lo(MI.getOperand(0)));
      if (Wide) {
        E.loadIndirect(1, K + 1);
        E.storeW(hi(MI.getOperand(0)));
      }
    });
    return true;
  }
  case ST8fi:
  case ST16fi: {
    int64_t Off = MI.getOperand(1).getImm() + MI.getOperand(2).getImm();
    bool Wide = Opc == ST16fi;
    E.withStackSlot(Off, Wide ? 2 : 1, [&](int64_t K) {
      E.loadW(lo(MI.getOperand(0)));
      E.storeIndirect(1, K);
      if (Wide) {
        E.loadW(hi(MI.getOperand(0)));
        E.storeIndirect(1, K + 1);
      }
    });
    return true;
  }
  case STI8fi:
  case STI16fi: {
    uint64_t K = MI.getOperand(0).getImm();
    int64_t Off = MI.getOperand(1).getImm() + MI.getOperand(2).getImm();
    bool Wide = Opc == STI16fi;
    E.withStackSlot(Off, Wide ? 2 : 1, [&](int64_t Slot) {
      E.loadWImm(K);
      E.storeIndirect(1, Slot);
      if (Wide) {
        E.loadWImm(K >> 8);
        E.storeIndirect(1, Slot + 1);
      }
    });
    return true;
  }
  case LEAfi: {
    int64_t Off = MI.getOperand(1).getImm() + MI.getOperand(2).getImm();
    unsigned DLo = lo(MI.getOperand(0)), DHi = hi(MI.getOperand(0));
    if (Off == 0) {
      E.copy(DLo, FSR1L_ADDR);
      E.copy(DHi, FSR1H_ADDR);
    } else {
      E.loadW(FSR1L_ADDR);
      E.opLit(ADDLW, Off);
      E.storeW(DLo);
      E.loadWImm(Off >> 8);
      E.opToW(ADDWFC, FSR1H_ADDR);
      E.storeW(DHi);
    }
    return true;
  }

  // --- The stack pointer and its cell -------------------------------------
  case SPLOAD: {
    MachineOperand Cell = MachineOperand::CreateGA(
        pic16f1StackPointerCell(*MI.getMF()->getFunction().getParent()), 0);
    E.loadDirect(FSR1L_ADDR, Cell, 0);
    E.loadDirect(FSR1H_ADDR, Cell, 1);
    return true;
  }
  case SPSTORE: {
    MachineOperand Cell = MachineOperand::CreateGA(
        pic16f1StackPointerCell(*MI.getMF()->getFunction().getParent()), 0);
    E.storeDirect(FSR1L_ADDR, Cell, 0);
    E.storeDirect(FSR1H_ADDR, Cell, 1);
    return true;
  }
  case SPSWITCH:
  case SPBACK: {
    MachineOperand Cell = MachineOperand::CreateGA(
        pic16f1StackPointerCell(*MI.getMF()->getFunction().getParent()), 0);
    const MachineOperand &Save = MI.getOperand(0);
    const MachineOperand &From = Opc == SPSWITCH ? Cell : Save;
    const MachineOperand &To = Opc == SPSWITCH ? Save : Cell;
    for (int B = 0; B != 2; ++B) {
      MachineOperand F = E.selectBank(From, B);
      E.build(MOVF).add(F).addImm(0);
      E.forgetW();
      MachineOperand T = E.selectBank(To, B);
      E.build(MOVWF).add(T);
    }
    if (Opc == SPSWITCH) {
      E.storeDirect(FSR1L_ADDR, Cell, 0);
      E.storeDirect(FSR1H_ADDR, Cell, 1);
    }
    return true;
  }

  // --- Calls and returns ---------------------------------------------------
  case CALLsym: {
    MachineOperand Page = MI.getOperand(0);
    Page.setTargetFlags(PIC16F1II::MO_PAGE);
    MachineOperand Target = MI.getOperand(0);
    Target.setTargetFlags(PIC16F1II::MO_NONE);
    if (MI.getMF()->getTarget().getCodeModel() != CodeModel::Small)
      E.build(MOVLP).add(Page);
    MachineInstrBuilder Call = E.build(CALL).add(Target);
    // Keep the register mask and implicit operands for later passes.
    for (const MachineOperand &MO : llvm::drop_begin(MI.operands()))
      Call.add(MO);
    E.forgetAll();
    E.reloadStackPointer(MI);
    return true;
  }
  case CALLreg: {
    E.loadW(hi(MI.getOperand(0)));
    E.storeW(PCLATH_ADDR);
    E.loadW(lo(MI.getOperand(0)));
    MachineInstrBuilder Call = E.build(CALLW);
    for (const MachineOperand &MO : llvm::drop_begin(MI.operands()))
      Call.add(MO);
    E.forgetAll();
    E.reloadStackPointer(MI);
    return true;
  }
  case RET: {
    // Keep the result registers on the instruction: the peephole pass
    // reads them to know what is still needed.
    MachineInstrBuilder Ret = E.build(RETURN);
    for (const MachineOperand &MO : MI.operands())
      if (MO.isReg() && MO.isImplicit())
        Ret.add(MO);
    return true;
  }
  case RETI:
    E.build(RETFIE);
    return true;
  }
}

/// The pseudos that need the stack pointer in FSR1 first: those that reach
/// the frame through it, and those that read or replace the cell.
static bool usesFrame(unsigned Opc) {
  switch (Opc) {
  case PIC16F1::LD8fi:
  case PIC16F1::LD16fi:
  case PIC16F1::ST8fi:
  case PIC16F1::ST16fi:
  case PIC16F1::STI8fi:
  case PIC16F1::STI16fi:
  case PIC16F1::LEAfi:
  case PIC16F1::SPSTORE:
  case PIC16F1::SPSWITCH:
  // SPBACK changes the cell that the stack pointer would be reloaded from.
  case PIC16F1::SPBACK:
    return true;
  default:
    return false;
  }
}

bool PIC16F1ExpandPseudo::runOnMachineFunction(MachineFunction &MF) {
  TII = MF.getSubtarget<PIC16F1Subtarget>().getInstrInfo();
  TRI = MF.getSubtarget().getRegisterInfo();
  bool Changed = false;
  Emitter E(*TII);
  E.Banks64 = MF.getSubtarget<PIC16F1Subtarget>().hasBanks64();
  E.UsesStackPointer = pic16f1UsesStackPointer(MF);

  // Once the pseudos are gone no instruction names the registers this
  // function changes, and the pass that collects them for callers
  // (interprocedural register allocation) runs later. Record them the way
  // a call's register mask would.
  {
    MachineRegisterInfo &MRI = MF.getRegInfo();
    std::vector<uint32_t> Kept(MachineOperand::getRegMaskSize(TRI->getNumRegs()),
                               ~uint32_t(0));
    for (unsigned Reg = 1, E = TRI->getNumRegs(); Reg != E; ++Reg)
      if (!MRI.def_empty(Reg))
        for (MCRegAliasIterator AI(Reg, TRI, true); AI.isValid(); ++AI)
          Kept[(*AI).id() / 32] &= ~(1u << ((*AI).id() % 32));
    MRI.addPhysRegsUsedFromRegMask(Kept.data());

    // A function compiled both ways: note what this version changes, for
    // the other to agree with (see getCalleeSavedRegs). If this is the
    // stack version and the static one came first, callers must expect
    // what either changes.
    const Function &F = MF.getFunction();
    if (F.hasFnAttribute("pic16f1-dual")) {
      auto &Table = MF.getSubtarget<PIC16F1Subtarget>().dualClobbers();
      BitVector Mine(TRI->getNumRegs());
      for (unsigned Reg = 1, E = TRI->getNumRegs(); Reg != E; ++Reg)
        if (MRI.isPhysRegModified(Reg))
          Mine.set(Reg);
      if (!F.hasFnAttribute("pic16f1-dual-of")) {
        // The stack version leaves FSR1 alone or loads the stack pointer
        // into it; the static versions use it as a pointer register. To
        // callers the function changes FSR1 whichever is linked.
        std::vector<uint32_t> Kept(
            MachineOperand::getRegMaskSize(TRI->getNumRegs()), ~uint32_t(0));
        for (unsigned Reg : {PIC16F1::SP, PIC16F1::FSR1L, PIC16F1::FSR1H}) {
          Kept[Reg / 32] &= ~(1u << (Reg % 32));
          Mine.set(Reg);
        }
        MRI.addPhysRegsUsedFromRegMask(Kept.data());
      }
      if (F.hasFnAttribute("pic16f1-dual-sf")) {
        const Function *Static = F.getParent()->getFunction(
            F.getFnAttribute("pic16f1-dual-sf").getValueAsString());
        auto It = Static ? Table.find(Static) : Table.end();
        if (It != Table.end()) {
          std::vector<uint32_t> Theirs(
              MachineOperand::getRegMaskSize(TRI->getNumRegs()), ~uint32_t(0));
          for (unsigned Reg : It->second.set_bits())
            Theirs[Reg / 32] &= ~(1u << (Reg % 32));
          MRI.addPhysRegsUsedFromRegMask(Theirs.data());
          Mine |= It->second;
        }
      }
      Table[&F] = Mine;
    }
  }

  for (MachineBasicBlock &MBB : MF) {
    E.forgetAll();
    for (MachineBasicBlock::iterator I = MBB.begin(); I != MBB.end();) {
      MachineInstr &MI = *I++;
      if (MI.isDebugInstr())
        continue;
      // The stack pointer must be in FSR1 for whatever reaches the frame
      // (the instructions of the epilogue among them, which are not
      // pseudos), and at the end of every block.
      if (E.StackPointerStale &&
          (!MI.isPseudo() || MI.isTerminator() || usesFrame(MI.getOpcode()))) {
        E.setInsertPoint(MBB, MachineBasicBlock::iterator(MI));
        E.needStackPointer(MF);
      }
      if (!MI.isPseudo()) {
        // An instruction this pass did not emit, from the prologue or
        // epilogue or inline assembly: assume it changes everything.
        E.forgetAll();
        continue;
      }
      if (MI.isTerminator() && MI.getOpcode() != PIC16F1::RET &&
          MI.getOpcode() != PIC16F1::RETI)
        continue;

      E.setInsertPoint(MBB, MachineBasicBlock::iterator(MI));
      bool Expanded = expand(MI, E);
      // FSR0 no longer matches its register pair if this instruction changed
      // the pair. Check after expanding: the instruction may itself have
      // pointed FSR0 at a pair that it then overwrites with its result.
      bool PointsFSR0 = MI.getOpcode() == PIC16F1::MOV16rr &&
                        MI.getOperand(0).getReg() == PIC16F1::FSR0;
      if (PointsFSR0)
        E.FSR0Holds = MI.getOperand(1).getReg();
      else if (MI.modifiesRegister(PIC16F1::FSR0, TRI))
        E.FSR0Holds = Register();
      if (E.FSR0Holds.isValid() && MI.modifiesRegister(E.FSR0Holds, TRI))
        E.FSR0Holds = Register();
      // The same for FSR1 where it is a register. A call leaves it alone,
      // but the stack pointer was put in it for the call.
      bool PointsFSR1 = MI.getOpcode() == PIC16F1::MOV16rr &&
                        MI.getOperand(0).getReg() == PIC16F1::SP;
      if (PointsFSR1)
        E.FSR1Holds = MI.getOperand(1).getReg();
      else if (MI.modifiesRegister(PIC16F1::SP, TRI))
        E.FSR1Holds = Register();
      if (E.FSR1Holds.isValid() && MI.modifiesRegister(E.FSR1Holds, TRI))
        E.FSR1Holds = Register();
      if (!Expanded) {
        // KILL, IMPLICIT_DEF and other target-independent leftovers.
        if (MI.getOpcode() == TargetOpcode::INLINEASM ||
            MI.getOpcode() == TargetOpcode::INLINEASM_BR)
          E.forgetAll();
        continue;
      }
      MI.eraseFromParent();
      Changed = true;
    }
    // A block that runs on into the next.
    if (E.StackPointerStale) {
      E.setInsertPoint(MBB, MBB.end());
      E.needStackPointer(MF);
      Changed = true;
    }
  }
  return Changed;
}

//===----------------------------------------------------------------------===//
// Branch expansion
//===----------------------------------------------------------------------===//

namespace {

class PIC16F1ExpandBranches : public MachineFunctionPass {
public:
  static char ID;
  PIC16F1ExpandBranches() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return "PIC16F1 branch expansion"; }
};

} // end anonymous namespace

char PIC16F1ExpandBranches::ID = 0;

INITIALIZE_PASS(PIC16F1ExpandBranches, "pic16f1-expand-branches",
                "PIC16F1 branch expansion", false, false)

FunctionPass *llvm::createPIC16F1ExpandBranchesPass() {
  return new PIC16F1ExpandBranches();
}

/// True if the instruction before \p Branch wrote its result to the file
/// register \p File and set Z from it.
static bool zFlagReflects(const MachineInstr &Branch, unsigned File) {
  const MachineBasicBlock &MBB = *Branch.getParent();
  MachineBasicBlock::const_iterator I(Branch);
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    switch (I->getOpcode()) {
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
    case PIC16F1::SUBWF:
    case PIC16F1::SUBWFB:
    case PIC16F1::XORWF:
      return I->getOperand(0).isImm() && I->getOperand(0).getImm() == File &&
             I->getOperand(1).getImm() == 1;
    default:
      return false;
    }
  }
  return false;
}

bool PIC16F1ExpandBranches::runOnMachineFunction(MachineFunction &MF) {
  const PIC16F1InstrInfo &TII =
      *MF.getSubtarget<PIC16F1Subtarget>().getInstrInfo();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineBasicBlock::iterator I = MBB.begin(); I != MBB.end();) {
      MachineInstr &MI = *I++;
      DebugLoc DL = MI.getDebugLoc();
      unsigned Opc = MI.getOpcode();
      if (Opc == PIC16F1::JMP) {
        BuildMI(MBB, MI, DL, TII.get(PIC16F1::BRA))
            .addMBB(MI.getOperand(0).getMBB());
      } else if (Opc == PIC16F1::JMPFAR) {
        MachineBasicBlock *Target = MI.getOperand(0).getMBB();
        if (MF.getTarget().getCodeModel() != CodeModel::Small)
          BuildMI(MBB, MI, DL, TII.get(PIC16F1::MOVLP))
              .addMBB(Target, PIC16F1II::MO_PAGE);
        BuildMI(MBB, MI, DL, TII.get(PIC16F1::GOTO)).addMBB(Target);
      } else if (Opc == PIC16F1::JMPreg) {
        // PCLATH takes the high byte; writing PCL then performs the jump.
        unsigned Pair = TII.fileAddr(MI.getOperand(0).getReg());
        BuildMI(MBB, MI, DL, TII.get(PIC16F1::MOVF)).addImm(Pair + 1).addImm(0);
        BuildMI(MBB, MI, DL, TII.get(PIC16F1::MOVWF))
            .addImm(PIC16F1::PCLATH_ADDR);
        BuildMI(MBB, MI, DL, TII.get(PIC16F1::MOVF)).addImm(Pair).addImm(0);
        BuildMI(MBB, MI, DL, TII.get(PIC16F1::MOVWF)).addImm(PIC16F1::PCL_ADDR);
      } else if (PIC16F1InstrInfo::isCompareBranch(Opc)) {
        SmallVector<PIC16F1RawInst, 12> Seq;
        TII.buildCompareBranch(MI, Seq);
        MachineBasicBlock *Target = MI.getOperand(3).getMBB();
        // A test of a byte for zero starts by moving it to W for the Z
        // flag. The flag is already right if the instruction before left
        // its result in that byte, as the DECF of a loop counter does.
        if (Seq.size() == 3 && Seq[0].Opc == PIC16F1::MOVF &&
            Seq[1].NumOps == 2 && Seq[1].Op0 == PIC16F1::STATUS_ADDR &&
            Seq[1].Op1 == PIC16F1::STATUS_Z && zFlagReflects(MI, Seq[0].Op0))
          Seq.erase(Seq.begin());
        for (const PIC16F1RawInst &R : Seq) {
          MachineInstrBuilder MIB = BuildMI(MBB, MI, DL, TII.get(R.Opc));
          if (R.IsBranch) {
            MIB.addMBB(Target);
            continue;
          }
          if (R.NumOps >= 1)
            MIB.addImm(R.Op0);
          if (R.NumOps >= 2)
            MIB.addImm(R.Op1);
        }
      } else {
        continue;
      }
      MI.eraseFromParent();
      Changed = true;
    }
  }
  return Changed;
}
