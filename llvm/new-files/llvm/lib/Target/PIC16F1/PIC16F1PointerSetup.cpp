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
#include "PIC16F1Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
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
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // end anonymous namespace

char PIC16F1PointerSetup::ID = 0;

INITIALIZE_PASS(PIC16F1PointerSetup, DEBUG_TYPE, "PIC16F1 pointer setup", false,
                false)

FunctionPass *llvm::createPIC16F1PointerSetupPass() {
  return new PIC16F1PointerSetup();
}

bool PIC16F1PointerSetup::runOnMachineFunction(MachineFunction &MF) {
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      switch (MI.getOpcode()) {
      default:
        continue;
      case PIC16F1::LD8ptr:
      case PIC16F1::LD16ptr:
      case PIC16F1::ST8ptr:
      case PIC16F1::ST16ptr:
      case PIC16F1::STI8ptr:
      case PIC16F1::STI16ptr:
        break;
      }
      // In all of these the pointer is operand 1.
      MachineOperand &Base = MI.getOperand(1);
      if (!Base.isReg() || Base.getReg() == PIC16F1::FSR0)
        continue;
      Register Ptr = Base.getReg();
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
