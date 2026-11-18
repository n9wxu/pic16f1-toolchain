//===-- PIC16F1ISelLowering.h - DAG lowering interface ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PIC16F1_PIC16F1ISELLOWERING_H
#define LLVM_LIB_TARGET_PIC16F1_PIC16F1ISELLOWERING_H

#include "PIC16F1.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {
class PIC16F1Subtarget;

class PIC16F1TargetLowering : public TargetLowering {
public:
  explicit PIC16F1TargetLowering(const TargetMachine &TM,
                                 const PIC16F1Subtarget &STI);

  MVT getScalarShiftAmountTy(const DataLayout &, EVT) const override {
    return MVT::i8;
  }
  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                         EVT VT) const override {
    return MVT::i8;
  }
  MVT::SimpleValueType getCmpLibcallReturnType() const override {
    return MVT::i16;
  }

  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                              MachineBasicBlock *BB) const override;

private:
  SDValue LowerShifts(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBlockAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerExternalSymbol(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBR_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG) const;

  MachineBasicBlock *emitSelect(MachineInstr &MI, MachineBasicBlock *BB) const;
  MachineBasicBlock *emitShiftLoop(MachineInstr &MI,
                                   MachineBasicBlock *BB) const;

  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                               bool isVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &dl, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;
  SDValue LowerCall(TargetLowering::CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;
  bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                      bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      LLVMContext &Context, const Type *RetTy) const override;
  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &dl,
                      SelectionDAG &DAG) const override;
};
} // namespace llvm

#endif
