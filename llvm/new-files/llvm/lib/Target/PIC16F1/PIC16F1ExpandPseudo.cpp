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
//    code it remembers, within a basic block, what W, BSR and FSR0 hold, and
//    leaves out instructions that would reload a value already there.
//
//  * PIC16F1ExpandBranches handles the branch pseudos. It runs after branch
//    relaxation, which needs each branch to still be one instruction.
//
// Inside an expansion W, FSR0, BSR, the Z and DC flags and (where the pseudo
// says so) TMP0 are scratch. The carry flag is preserved by every expansion
// that does not list it as defined, because wide add and subtract chains keep
// it live from one pseudo to the next.
//
//===----------------------------------------------------------------------===//

#include "PIC16F1.h"
#include "PIC16F1InstrInfo.h"
#include "PIC16F1Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
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
  // FSR0 equals this register pair, or no register.
  Register FSR0Holds;

  explicit Emitter(const PIC16F1InstrInfo &TII) : TII(TII) {}

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

  /// Select the bank of the address in \p Addr plus \p Extra and return the
  /// file-register operand for it.
  MachineOperand selectBank(const MachineOperand &Addr, int64_t Extra) {
    if (Addr.isImm()) {
      uint64_t A = uint64_t(Addr.getImm() + Extra) & 0xFFF;
      unsigned F = A & 0x7F;
      int Bank = A >> 7;
      if (!isUnbanked(F) && !(TrackValues && BSRConst == Bank)) {
        build(PIC16F1::MOVLB).addImm(Bank);
        BSRConst = Bank;
        BSRGlobal = nullptr;
      }
      return MachineOperand::CreateImm(F);
    }
    assert(Addr.isGlobal() && "direct address must be constant or global");
    const GlobalValue *GV = Addr.getGlobal();
    int64_t Off = Addr.getOffset() + Extra;
    if (!(TrackValues && BSRGlobal == GV && BSROffset == Off)) {
      build(PIC16F1::MOVLB)
          .add(MachineOperand::CreateGA(GV, Off, PIC16F1II::MO_BANK));
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

  // --- Pointer access through FSR0 -----------------------------------------

  void pointFSR0At(Register Pair, unsigned PairAddr) {
    if (TrackValues && FSR0Holds == Pair)
      return;
    loadW(PairAddr);
    storeW(PIC16F1::FSR0L_ADDR);
    loadW(PairAddr + 1);
    storeW(PIC16F1::FSR0H_ADDR);
    FSR0Holds = Pair;
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
    uint64_t K = MI.getOperand(2).getImm();
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

  // --- Loads and stores through a pointer ----------------------------------
  case LD8ptr:
  case LD16ptr: {
    int64_t Off = MI.getOperand(2).getImm();
    E.pointFSR0At(MI.getOperand(1).getReg(), lo(MI.getOperand(1)));
    E.loadIndirect(0, Off);
    E.storeW(lo(MI.getOperand(0)));
    if (Opc == LD16ptr) {
      E.loadIndirect(0, Off + 1);
      E.storeW(hi(MI.getOperand(0)));
    }
    return true;
  }
  case ST8ptr:
  case ST16ptr: {
    int64_t Off = MI.getOperand(2).getImm();
    E.pointFSR0At(MI.getOperand(1).getReg(), lo(MI.getOperand(1)));
    E.loadW(lo(MI.getOperand(0)));
    E.storeIndirect(0, Off);
    if (Opc == ST16ptr) {
      E.loadW(hi(MI.getOperand(0)));
      E.storeIndirect(0, Off + 1);
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

  // --- Calls and returns ---------------------------------------------------
  case CALLsym: {
    MachineOperand Page = MI.getOperand(0);
    Page.setTargetFlags(PIC16F1II::MO_PAGE);
    MachineOperand Target = MI.getOperand(0);
    Target.setTargetFlags(PIC16F1II::MO_NONE);
    E.build(MOVLP).add(Page);
    MachineInstrBuilder Call = E.build(CALL).add(Target);
    // Keep the register mask and implicit operands for later passes.
    for (const MachineOperand &MO : llvm::drop_begin(MI.operands()))
      Call.add(MO);
    E.forgetAll();
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
    return true;
  }
  case RET:
    E.build(RETURN);
    return true;
  case RETI:
    E.build(RETFIE);
    return true;
  }
}

bool PIC16F1ExpandPseudo::runOnMachineFunction(MachineFunction &MF) {
  TII = MF.getSubtarget<PIC16F1Subtarget>().getInstrInfo();
  TRI = MF.getSubtarget().getRegisterInfo();
  bool Changed = false;
  Emitter E(*TII);

  for (MachineBasicBlock &MBB : MF) {
    E.forgetAll();
    for (MachineBasicBlock::iterator I = MBB.begin(); I != MBB.end();) {
      MachineInstr &MI = *I++;
      if (MI.isDebugInstr())
        continue;
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
      if (E.FSR0Holds.isValid() && MI.modifiesRegister(E.FSR0Holds, TRI))
        E.FSR0Holds = Register();
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
        BuildMI(MBB, MI, DL, TII.get(PIC16F1::MOVLP))
            .addMBB(Target, PIC16F1II::MO_PAGE);
        BuildMI(MBB, MI, DL, TII.get(PIC16F1::GOTO)).addMBB(Target);
      } else if (PIC16F1InstrInfo::isCompareBranch(Opc)) {
        SmallVector<PIC16F1RawInst, 12> Seq;
        TII.buildCompareBranch(MI, Seq);
        MachineBasicBlock *Target = MI.getOperand(3).getMBB();
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
