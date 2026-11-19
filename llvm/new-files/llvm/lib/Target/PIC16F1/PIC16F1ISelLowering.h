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
#include "llvm/CodeGen/CallingConvLower.h"
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
                         EVT VT) const override;
  /// GlobalISel's inline-assembly lowering handles an operand only when it
  /// fits one register; a wider one goes to SelectionDAG.
  bool fallBackToDAGISel(const Instruction &Inst) const override;

  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;
  /// Division is a library call, but the alternative, a multiply by a
  /// reciprocal, needs the high half of a double-width product, which costs
  /// far more here.
  bool isIntDivCheap(EVT VT, AttributeList Attr) const override { return true; }

  /// What an address can be made of: a constant address, or a pointer plus
  /// a small constant (MOVIW k[FSRn]). There is no indexed mode, which tells
  /// the loop optimizer to step a pointer through an array instead of
  /// adding the index to its address on every round.
  bool isLegalAddressingMode(const DataLayout &DL, const AddrMode &AM,
                             Type *Ty, unsigned AddrSpace,
                             Instruction *I = nullptr) const override {
    if (AM.Scale != 0)
      return false;
    if (AM.BaseGV)
      return !AM.HasBaseReg;
    return AM.BaseOffs >= -32 && AM.BaseOffs <= 30;
  }

  /// "x + 1 overflowed" is cheaper here as an add and a compare with zero
  /// than as an add whose carry is turned into a value.
  bool shouldFormOverflowOp(unsigned Opcode, EVT VT,
                            bool MathUsed) const override {
    return false;
  }

  MVT::SimpleValueType getCmpLibcallReturnType() const override {
    return MVT::i16;
  }

  /// The calling convention's assignment functions, for GlobalISel.
  CCAssignFn *getCCAssignFn(bool IsVarArg) const;
  CCAssignFn *getRetCCAssignFn() const;

  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;
  void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue> &Results,
                          SelectionDAG &DAG) const override;

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
