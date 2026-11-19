//===-- PIC16F1CallLowering.cpp - Call lowering for GlobalISel ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//
// The calling convention itself is in PIC16F1CallingConv.td and is specified
// in docs/spec/calling-convention.md. This file applies it to generic
// machine IR.
//
//===----------------------------------------------------------------------===//

#include "PIC16F1CallLowering.h"
#include "PIC16F1.h"
#include "PIC16F1ISelLowering.h"
#include "PIC16F1InstrInfo.h"
#include "PIC16F1MachineFunctionInfo.h"
#include "PIC16F1Subtarget.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/FunctionLoweringInfo.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/TargetCallingConv.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

PIC16F1CallLowering::PIC16F1CallLowering(const PIC16F1TargetLowering &TLI)
    : CallLowering(&TLI) {}

namespace {

/// Values arriving in registers or on the stack: formal arguments, and the
/// result of a call.
struct IncomingHandler : public CallLowering::IncomingValueHandler {
  IncomingHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI)
      : IncomingValueHandler(MIRBuilder, MRI) {}

  void assignValueToAddress(Register ValVReg, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override {
    MachineFunction &MF = MIRBuilder.getMF();
    auto *MMO = MF.getMachineMemOperand(MPO, MachineMemOperand::MOLoad, MemTy,
                                        Align(1));
    MIRBuilder.buildLoad(ValVReg, Addr, *MMO);
  }

  Register getStackAddress(uint64_t Size, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override {
    MachineFunction &MF = MIRBuilder.getMF();
    int FI = MF.getFrameInfo().CreateFixedObject(Size, Offset, true);
    MPO = MachinePointerInfo::getFixedStack(MF, FI);
    return MIRBuilder.buildFrameIndex(LLT::pointer(0, 16), FI).getReg(0);
  }
};

struct FormalArgHandler : public IncomingHandler {
  using IncomingHandler::IncomingHandler;

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA,
                        ISD::ArgFlagsTy Flags = {}) override {
    MIRBuilder.getMRI()->addLiveIn(PhysReg);
    MIRBuilder.getMBB().addLiveIn(PhysReg);
    IncomingValueHandler::assignValueToReg(ValVReg, PhysReg, VA);
  }
};

struct CallResultHandler : public IncomingHandler {
  CallResultHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI,
                    MachineInstrBuilder &MIB)
      : IncomingHandler(MIRBuilder, MRI), MIB(MIB) {}

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA,
                        ISD::ArgFlagsTy Flags = {}) override {
    MIB.addDef(PhysReg, RegState::Implicit);
    IncomingValueHandler::assignValueToReg(ValVReg, PhysReg, VA);
  }

  MachineInstrBuilder &MIB;
};

/// Values leaving in registers or on the stack: call arguments and return
/// values.
struct OutgoingHandler : public CallLowering::OutgoingValueHandler {
  OutgoingHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI,
                  MachineInstrBuilder &MIB)
      : OutgoingValueHandler(MIRBuilder, MRI), MIB(MIB) {}

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA,
                        ISD::ArgFlagsTy Flags = {}) override {
    MIB.addUse(PhysReg, RegState::Implicit);
    MIRBuilder.buildCopy(PhysReg, extendRegister(ValVReg, VA));
  }

  /// A stack argument is stored at a constant offset from the stack
  /// pointer: the outgoing-argument area is the bottom of the caller's
  /// frame, reserved by the prologue. No address is computed, so this
  /// returns no register and assignValueToAddress uses the offset.
  Register getStackAddress(uint64_t Size, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override {
    MPO = MachinePointerInfo::getStack(MIRBuilder.getMF(), Offset);
    PendingOffset = Offset;
    return Register();
  }

  void assignValueToAddress(Register ValVReg, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override {
    unsigned Opc = MemTy.getSizeInBits() == 8 ? PIC16F1::ST8fi : PIC16F1::ST16fi;
    MIRBuilder.buildInstr(Opc)
        .addUse(extendRegister(ValVReg, VA))
        .addImm(PendingOffset)
        .addImm(0);
  }

  MachineInstrBuilder &MIB;
  int64_t PendingOffset = 0;
};

} // end anonymous namespace

/// A function with a static frame is compiled without a stack pointer
/// (PIC16F1StaticFrames.cpp), which was decided from its source. If code
/// generation finds it needs the stack after all, stop rather than emit
/// code that scribbles through FSR1.
static void checkNoStack(const MachineFunction &MF, uint64_t StackBytes) {
  if (StackBytes != 0 && MF.getFunction().hasFnAttribute("pic16f1-static-frame"))
    report_fatal_error("PIC16F1: function '" + MF.getName() +
                       "' has a static frame but passes arguments on the "
                       "stack; build with -mllvm -pic16f1-static-frames=0");
}

bool PIC16F1CallLowering::canLowerReturn(MachineFunction &MF,
                                         CallingConv::ID CallConv,
                                         SmallVectorImpl<BaseArgInfo> &Outs,
                                         bool IsVarArg) const {
  const auto &TLI = *getTLI<PIC16F1TargetLowering>();
  SmallVector<CCValAssign, 16> Locs;
  CCState CCInfo(CallConv, IsVarArg, MF, Locs, MF.getFunction().getContext());
  return checkReturn(CCInfo, Outs, TLI.getRetCCAssignFn());
}

bool PIC16F1CallLowering::lowerReturn(MachineIRBuilder &MIRBuilder,
                                      const Value *Val,
                                      ArrayRef<Register> VRegs,
                                      FunctionLoweringInfo &FLI) const {
  MachineFunction &MF = MIRBuilder.getMF();
  const Function &F = MF.getFunction();
  const auto &TLI = *getTLI<PIC16F1TargetLowering>();
  bool IsInterrupt = F.hasFnAttribute("interrupt");

  // A result too large for the result registers would need a hidden
  // pointer argument; Clang never produces one for this target.
  if (!FLI.CanLowerReturn)
    return false;

  auto MIB = MIRBuilder.buildInstrNoInsert(IsInterrupt ? PIC16F1::RETI
                                                       : PIC16F1::RET);
  if (!VRegs.empty()) {
    if (IsInterrupt)
      return false;
    const DataLayout &DL = F.getDataLayout();
    SmallVector<ArgInfo, 8> SplitArgs;
    ArgInfo OrigArg{VRegs, Val->getType(), 0};
    setArgFlags(OrigArg, AttributeList::ReturnIndex, DL, F);
    splitToValueTypes(OrigArg, SplitArgs, DL, F.getCallingConv());
    OutgoingValueAssigner Assigner(TLI.getRetCCAssignFn());
    OutgoingHandler Handler(MIRBuilder, MF.getRegInfo(), MIB);
    if (!determineAndHandleAssignments(Handler, Assigner, SplitArgs, MIRBuilder,
                                       F.getCallingConv(), F.isVarArg()))
      return false;
  }
  MIRBuilder.insertInstr(MIB);
  return true;
}

bool PIC16F1CallLowering::lowerFormalArguments(
    MachineIRBuilder &MIRBuilder, const Function &F,
    ArrayRef<ArrayRef<Register>> VRegs, FunctionLoweringInfo &FLI) const {
  MachineFunction &MF = MIRBuilder.getMF();
  const DataLayout &DL = F.getDataLayout();
  const auto &TLI = *getTLI<PIC16F1TargetLowering>();

  if (!FLI.CanLowerReturn)
    return false;
  if (F.hasFnAttribute("interrupt") && !F.arg_empty())
    return false;

  SmallVector<ArgInfo, 8> SplitArgs;
  unsigned I = 0;
  for (const Argument &Arg : F.args()) {
    if (Arg.hasByValAttr())
      return false;
    ArgInfo OrigArg{VRegs[I], Arg.getType(), I};
    setArgFlags(OrigArg, I + AttributeList::FirstArgIndex, DL, F);
    splitToValueTypes(OrigArg, SplitArgs, DL, F.getCallingConv());
    ++I;
  }

  IncomingValueAssigner Assigner(TLI.getCCAssignFn(F.isVarArg()));
  FormalArgHandler Handler(MIRBuilder, MF.getRegInfo());
  if (!determineAndHandleAssignments(Handler, Assigner, SplitArgs, MIRBuilder,
                                     F.getCallingConv(), F.isVarArg()))
    return false;

  checkNoStack(MF, Assigner.StackSize);

  // The variable arguments start just past the named ones.
  if (F.isVarArg()) {
    int FI = MF.getFrameInfo().CreateFixedObject(1, Assigner.StackSize, true);
    MF.getInfo<PIC16F1MachineFunctionInfo>()->setVarArgsFrameIndex(FI);
  }
  return true;
}

bool PIC16F1CallLowering::lowerCall(MachineIRBuilder &MIRBuilder,
                                    CallLoweringInfo &Info) const {
  MachineFunction &MF = MIRBuilder.getMF();
  const Function &F = MF.getFunction();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const DataLayout &DL = F.getDataLayout();
  const auto &TLI = *getTLI<PIC16F1TargetLowering>();
  const auto &STI = MF.getSubtarget<PIC16F1Subtarget>();
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  const TargetRegisterInfo *TRI = STI.getRegisterInfo();

  if (Info.IsMustTailCall || !Info.CanLowerReturn)
    return false;
  Info.IsTailCall = false;

  SmallVector<ArgInfo, 8> OutArgs;
  for (auto &OrigArg : Info.OrigArgs) {
    if (OrigArg.Flags[0].isByVal())
      return false;
    splitToValueTypes(OrigArg, OutArgs, DL, Info.CallConv);
  }
  SmallVector<ArgInfo, 8> InArgs;
  if (!Info.OrigRet.Ty->isVoidTy())
    splitToValueTypes(Info.OrigRet, InArgs, DL, Info.CallConv);

  auto CallSeqStart = MIRBuilder.buildInstr(TII.getCallFrameSetupOpcode());

  const uint32_t *Preserved = TRI->getCallPreservedMask(MF, Info.CallConv);
  if (Info.Callee.isSymbol())
    if (const uint32_t *More = PIC16F1RegisterInfo::getLibcallPreservedMask(
            Info.Callee.getSymbolName()))
      Preserved = More;
  if (Info.Callee.isGlobal())
    if (const auto *Callee = dyn_cast<Function>(Info.Callee.getGlobal()))
      if (Callee->hasFnAttribute("pic16f1-isr-of-local"))
        Preserved = PIC16F1RegisterInfo::getInterruptCopyMask();
  auto MIB = MIRBuilder
                 .buildInstrNoInsert(Info.Callee.isReg() ? PIC16F1::CALLreg
                                                         : PIC16F1::CALLsym)
                 .add(Info.Callee)
                 .addRegMask(Preserved);

  OutgoingValueAssigner Assigner(TLI.getCCAssignFn(Info.IsVarArg));
  OutgoingHandler Handler(MIRBuilder, MRI, MIB);
  if (!determineAndHandleAssignments(Handler, Assigner, OutArgs, MIRBuilder,
                                     Info.CallConv, Info.IsVarArg))
    return false;

  if (Info.Callee.isReg())
    constrainOperandRegClass(MF, *TRI, MRI, TII, *STI.getRegBankInfo(), *MIB,
                             MIB->getDesc(), Info.Callee, 0);
  checkNoStack(MF, Assigner.StackSize);
  MIRBuilder.insertInstr(MIB);

  if (!Info.OrigRet.Ty->isVoidTy()) {
    IncomingValueAssigner RetAssigner(TLI.getRetCCAssignFn());
    CallResultHandler RetHandler(MIRBuilder, MRI, MIB);
    if (!determineAndHandleAssignments(RetHandler, RetAssigner, InArgs,
                                       MIRBuilder, Info.CallConv,
                                       Info.IsVarArg))
      return false;
  }

  CallSeqStart.addImm(Assigner.StackSize).addImm(0);
  MIRBuilder.buildInstr(TII.getCallFrameDestroyOpcode())
      .addImm(Assigner.StackSize)
      .addImm(0);
  return true;
}
