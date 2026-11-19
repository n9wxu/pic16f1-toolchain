//===-- PIC16F1PointerSetup.cpp - Put pointers in FSR0 before use ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A load or store through a pointer needs the pointer in FSR0. Instruction
// selection leaves the pointer in a virtual register; this pass, just before
// register allocation, copies it to FSR0 in front of each such instruction
// and makes the instruction use FSR0. The copy is a hint: the allocator
// gives the pointer FSR0 itself where nothing else needs FSR0 meanwhile,
// and the copy then disappears.
//
//===----------------------------------------------------------------------===//

#include "PIC16F1.h"
#include "PIC16F1InstrInfo.h"
#include "PIC16F1MachineFunctionInfo.h"
#include "PIC16F1Subtarget.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

#define DEBUG_TYPE "pic16f1-pointer-setup"

namespace {

class PIC16F1PointerSetup : public MachineFunctionPass {
public:
  static char ID;
  PIC16F1PointerSetup() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return "PIC16F1 pointer setup"; }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    AU.addPreserved<MachineLoopInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // end anonymous namespace

char PIC16F1PointerSetup::ID = 0;

INITIALIZE_PASS_BEGIN(PIC16F1PointerSetup, DEBUG_TYPE, "PIC16F1 pointer setup",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_END(PIC16F1PointerSetup, DEBUG_TYPE, "PIC16F1 pointer setup",
                    false, false)

FunctionPass *llvm::createPIC16F1PointerSetupPass() {
  return new PIC16F1PointerSetup();
}

static bool isPointerAccess(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case PIC16F1::LD8ptr:
  case PIC16F1::LD16ptr:
  case PIC16F1::ST8ptr:
  case PIC16F1::ST16ptr:
  case PIC16F1::STI8ptr:
  case PIC16F1::STI16ptr:
    return true;
  default:
    return false;
  }
}

/// The form of a pointer access that takes its pointer in FSR \p N only.
static unsigned inFSRForm(unsigned Opc, unsigned N) {
  switch (Opc) {
  case PIC16F1::LD8ptr:   return N ? PIC16F1::LD8ptr1 : PIC16F1::LD8ptr0;
  case PIC16F1::LD16ptr:  return N ? PIC16F1::LD16ptr1 : PIC16F1::LD16ptr0;
  case PIC16F1::ST8ptr:   return N ? PIC16F1::ST8ptr1 : PIC16F1::ST8ptr0;
  case PIC16F1::ST16ptr:  return N ? PIC16F1::ST16ptr1 : PIC16F1::ST16ptr0;
  case PIC16F1::STI8ptr:  return N ? PIC16F1::STI8ptr1 : PIC16F1::STI8ptr0;
  case PIC16F1::STI16ptr: return N ? PIC16F1::STI16ptr1 : PIC16F1::STI16ptr0;
  default:
    llvm_unreachable("not a pointer access");
  }
}

/// p2 = p + k in front of an access through p keeps both alive at once, so
/// they cannot share a register: move the addition to just after the last
/// use of p in its block. Then p ends where p2 begins, and a pointer that
/// lives in an FSR is stepped there (and the step joins the access as
/// MOVIW FSR0++).
static bool sinkIncrements(MachineBasicBlock &MBB, MachineRegisterInfo &MRI,
                           const TargetRegisterInfo &TRI) {
  bool Changed = false;
  for (auto It = MBB.begin(); It != MBB.end();) {
    MachineInstr &Add = *It++;
    if (Add.getOpcode() != PIC16F1::ADD16ri || !Add.getOperand(1).isReg() ||
        !Add.getOperand(2).isImm())
      continue;
    Register P = Add.getOperand(1).getReg(), P2 = Add.getOperand(0).getReg();
    if (!P.isVirtual() || !P2.isVirtual())
      continue;
    // The last use of p after the addition, with nothing on the way that
    // uses p2 or the carry the addition sets.
    MachineInstr *Last = nullptr;
    bool SawAccess = false;
    for (auto J = std::next(Add.getIterator()); J != MBB.end(); ++J) {
      if (J->isTerminator() || J->readsRegister(P2, &TRI) ||
          J->readsRegister(PIC16F1::CARRY, &TRI))
        break;
      if (J->readsRegister(P, &TRI)) {
        Last = &*J;
        SawAccess |= isPointerAccess(*J);
      }
    }
    if (!Last || !SawAccess)
      continue;
    // The carry must be free where the addition lands.
    auto After = std::next(Last->getIterator());
    bool CarryLive = false;
    for (auto J = After; J != MBB.end(); ++J) {
      if (J->readsRegister(PIC16F1::CARRY, &TRI)) {
        CarryLive = true;
        break;
      }
      if (J->modifiesRegister(PIC16F1::CARRY, &TRI))
        break;
    }
    if (CarryLive)
      continue;
    if (It == After)
      ++It;
    MBB.splice(After, &MBB, Add.getIterator());
    MRI.clearKillFlags(P);
    Changed = true;
  }
  return Changed;
}

bool PIC16F1PointerSetup::runOnMachineFunction(MachineFunction &MF) {
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const MachineLoopInfo &Loops =
      getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF)
    Changed |= sinkIncrements(MBB, MRI, TRI);

  // A pointer that a loop keeps from round to round (it is defined outside
  // the loop, or carried round it) should stay in an FSR for the whole
  // loop. With only a hint to go on the register allocator often puts it
  // in a register pair and copies it for each access, so the choice is made
  // here and the accesses become forms that take their pointer in that FSR
  // only:
  //  - If the loop also has accesses through addresses worked out in it,
  //    those need FSR0; a kept pointer can have FSR1, where FSR1 is a
  //    register (a function with a static frame). Moving the stack pointer
  //    out of FSR1 and back costs eight instructions, so when optimizing
  //    for size only a pointer used at two places or more.
  //  - Otherwise the most used kept pointer has FSR0, and the next FSR1.
  // Not in a loop with calls: they change FSR0 and need the stack pointer
  // in FSR1.
  bool FSR1Free = MF.getInfo<PIC16F1MachineFunctionInfo>()->fsr1IsRegister();
  SmallSet<Register, 4> InFSR0, InFSR1;
  {
    struct Use {
      MapVector<Register, unsigned> Kept;
      unsigned Computed = 0;
    };
    MapVector<const MachineLoop *, Use> PerLoop;
    SmallPtrSet<const MachineLoop *, 4> HasCall;
    for (const MachineBasicBlock &MBB : MF) {
      const MachineLoop *L = Loops.getLoopFor(&MBB);
      if (!L)
        continue;
      for (const MachineInstr &MI : MBB) {
        if (MI.isCall())
          for (const MachineLoop *Outer = L; Outer;
               Outer = Outer->getParentLoop())
            HasCall.insert(Outer);
        if (!isPointerAccess(MI) || !MI.getOperand(1).isReg() ||
            !MI.getOperand(1).getReg().isVirtual())
          continue;
        Register Ptr = MI.getOperand(1).getReg();
        const MachineInstr *Def = MRI.getUniqueVRegDef(Ptr);
        bool Kept = Def && (!L->contains(Def->getParent()) ||
                            (Def->isPHI() && Def->getParent() == L->getHeader()));
        if (Kept)
          ++PerLoop[L].Kept[Ptr];
        else
          ++PerLoop[L].Computed;
      }
    }
    bool ForSize = MF.getFunction().hasOptSize();
    for (auto &[L, U] : PerLoop) {
      if (HasCall.count(L))
        continue;
      SmallVector<std::pair<Register, unsigned>, 4> Kept(U.Kept.begin(),
                                                         U.Kept.end());
      llvm::stable_sort(Kept, [](const auto &A, const auto &B) {
        return A.second > B.second;
      });
      if (U.Computed == 0) {
        if (!Kept.empty())
          InFSR0.insert(Kept[0].first);
        if (Kept.size() >= 2 && FSR1Free)
          InFSR1.insert(Kept[1].first);
      } else if (FSR1Free && !Kept.empty() &&
                 (Kept[0].second >= 2 || !ForSize)) {
        InFSR1.insert(Kept[0].first);
      }
    }
    // One FSR per pointer: a loop's choice for FSR1 stands.
    for (Register R : InFSR1)
      InFSR0.erase(R);
  }

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (!isPointerAccess(MI))
        continue;
      // In all of these the pointer is operand 1.
      MachineOperand &Base = MI.getOperand(1);
      if (!Base.isReg() || Base.getReg() == PIC16F1::FSR0 ||
          Base.getReg() == PIC16F1::SP)
        continue;
      Register Ptr = Base.getReg();
      if ((InFSR0.count(Ptr) || InFSR1.count(Ptr)) && !Base.getSubReg()) {
        // The pointer itself is held to the FSR, and what it is copied
        // from and to follows when the copies are merged.
        unsigned N = InFSR1.count(Ptr);
        const TargetRegisterClass *RC =
            N ? &PIC16F1::FSR1CRegClass : &PIC16F1::FSR0CRegClass;
        MI.setDesc(TII.get(inFSRForm(MI.getOpcode(), N)));
        if (!MRI.constrainRegClass(Ptr, RC)) {
          Register In = MRI.createVirtualRegister(RC);
          BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(TargetOpcode::COPY), In)
              .addReg(Ptr);
          Base.setReg(In);
          Base.setIsKill(true);
          MRI.clearKillFlags(Ptr);
        }
        Changed = true;
        continue;
      }
      BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(TargetOpcode::COPY),
              PIC16F1::FSR0)
          .addReg(Ptr, {}, Base.getSubReg());
      Base.setSubReg(0);
      Base.setReg(PIC16F1::FSR0);
      Base.setIsKill(true);
      // The pointer is now read one instruction earlier; later passes work
      // out again where it dies.
      if (Ptr.isVirtual())
        MRI.clearKillFlags(Ptr);
      Changed = true;
    }
  }
  return Changed;
}
