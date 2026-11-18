//===-- PIC16F1ISelLowering.cpp - DAG lowering ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PIC16F1ISelLowering.h"
#include "PIC16F1.h"
#include "PIC16F1MachineFunctionInfo.h"
#include "PIC16F1SelectionDAGInfo.h"
#include "PIC16F1Subtarget.h"
#include "PIC16F1TargetMachine.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "pic16f1-lower"

PIC16F1TargetLowering::PIC16F1TargetLowering(const TargetMachine &TM,
                                             const PIC16F1Subtarget &STI)
    : TargetLowering(TM, STI) {
  addRegisterClass(MVT::i8, &PIC16F1::GPR8RegClass);
  addRegisterClass(MVT::i16, &PIC16F1::GPR16RegClass);
  computeRegisterProperties(STI.getRegisterInfo());

  setStackPointerRegisterToSaveRestore(PIC16F1::SP);
  setBooleanContents(ZeroOrOneBooleanContent);
  setBooleanVectorContents(ZeroOrOneBooleanContent);
  setMinFunctionAlignment(Align(2));
  setPrefFunctionAlignment(Align(2));
  setMinimumJumpTableEntries(UINT_MAX);
  setMaxAtomicSizeInBitsSupported(0);

  for (MVT VT : MVT::integer_valuetypes()) {
    for (auto Ext : {ISD::EXTLOAD, ISD::SEXTLOAD, ISD::ZEXTLOAD}) {
      setLoadExtAction(Ext, VT, MVT::i1, Promote);
      setLoadExtAction(Ext, VT, MVT::i8, Expand);
      setLoadExtAction(Ext, VT, MVT::i16, Expand);
    }
  }
  setTruncStoreAction(MVT::i16, MVT::i8, Expand);

  for (MVT VT : {MVT::i8, MVT::i16}) {
    setOperationAction({ISD::SHL, ISD::SRL, ISD::SRA}, VT, Custom);
    setOperationAction({ISD::ROTL, ISD::ROTR}, VT, Expand);
    setOperationAction({ISD::SHL_PARTS, ISD::SRL_PARTS, ISD::SRA_PARTS}, VT,
                       Expand);
    setOperationAction(ISD::BR_CC, VT, Custom);
    setOperationAction(ISD::SELECT_CC, VT, Custom);
    setOperationAction(ISD::SELECT, VT, Expand);
    setOperationAction(ISD::SETCC, VT, Expand);
    setOperationAction({ISD::MUL, ISD::SDIV, ISD::UDIV, ISD::SREM, ISD::UREM},
                       VT, LibCall);
    setOperationAction({ISD::MULHS, ISD::MULHU, ISD::SMUL_LOHI, ISD::UMUL_LOHI,
                        ISD::SDIVREM, ISD::UDIVREM},
                       VT, Expand);
    setOperationAction({ISD::CTPOP, ISD::CTLZ, ISD::CTTZ, ISD::BSWAP,
                        ISD::BITREVERSE, ISD::PARITY, ISD::ABS},
                       VT, Expand);
    setOperationAction({ISD::SMIN, ISD::SMAX, ISD::UMIN, ISD::UMAX}, VT,
                       Expand);
    setOperationAction({ISD::UADDO, ISD::USUBO, ISD::SADDO, ISD::SSUBO,
                        ISD::UMULO, ISD::SMULO, ISD::UADDO_CARRY,
                        ISD::USUBO_CARRY, ISD::UADDSAT, ISD::USUBSAT,
                        ISD::SADDSAT, ISD::SSUBSAT},
                       VT, Expand);
    setOperationAction(ISD::DYNAMIC_STACKALLOC, VT, Custom);
  }
  // 32- and 64-bit shifts by a constant: see ReplaceNodeResults.
  setOperationAction({ISD::SHL, ISD::SRL, ISD::SRA}, {MVT::i32, MVT::i64},
                     Custom);
  // Wider add and subtract chain through the carry flag in 16-bit steps.
  setOperationAction({ISD::ADDC, ISD::ADDE, ISD::SUBC, ISD::SUBE}, MVT::i16,
                     Legal);
  setOperationAction({ISD::ADDC, ISD::ADDE, ISD::SUBC, ISD::SUBE}, MVT::i8,
                     Expand);

  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);
  setOperationAction({ISD::GlobalAddress, ISD::ExternalSymbol,
                      ISD::BlockAddress},
                     MVT::i16, Custom);
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);
  setOperationAction({ISD::STACKSAVE, ISD::STACKRESTORE}, MVT::Other, Expand);
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction({ISD::VAARG, ISD::VAEND, ISD::VACOPY}, MVT::Other, Expand);

  // Inline byte loops are shorter than a call only for very small sizes.
  MaxStoresPerMemset = MaxStoresPerMemsetOptSize = 4;
  MaxStoresPerMemcpy = MaxStoresPerMemcpyOptSize = 4;
  MaxStoresPerMemmove = MaxStoresPerMemmoveOptSize = 4;
}

SDValue PIC16F1TargetLowering::LowerOperation(SDValue Op,
                                              SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::SHL:
  case ISD::SRL:
  case ISD::SRA:
    return LowerShifts(Op, DAG);
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::BlockAddress:
    return LowerBlockAddress(Op, DAG);
  case ISD::ExternalSymbol:
    return LowerExternalSymbol(Op, DAG);
  case ISD::BR_CC:
    return LowerBR_CC(Op, DAG);
  case ISD::SELECT_CC:
    return LowerSELECT_CC(Op, DAG);
  case ISD::VASTART:
    return LowerVASTART(Op, DAG);
  case ISD::DYNAMIC_STACKALLOC:
    // Stack slots are addressed from the stack pointer, so it cannot move
    // while a function runs.
    report_fatal_error(
        "variable-length arrays and alloca are not supported on PIC16F1");
  default:
    llvm_unreachable("unimplemented operand");
  }
}

EVT PIC16F1TargetLowering::getSetCCResultType(const DataLayout &DL,
                                              LLVMContext &Context,
                                              EVT VT) const {
  if (VT.isVector())
    return VT.changeVectorElementTypeToInteger();
  return MVT::i8;
}

std::pair<unsigned, const TargetRegisterClass *>
PIC16F1TargetLowering::getRegForInlineAsmConstraint(
    const TargetRegisterInfo *TRI, StringRef Constraint, MVT VT) const {
  if (Constraint == "r") {
    // A byte takes one register. Anything wider is split over 16-bit pairs.
    if (VT == MVT::i8)
      return {0U, &PIC16F1::GPR8RegClass};
    return {0U, &PIC16F1::GPR16RegClass};
  }
  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

//===----------------------------------------------------------------------===//
// Calling convention
//===----------------------------------------------------------------------===//

#define GET_CALLING_CONV_IMPL
#include "PIC16F1GenCallingConv.inc"

CCAssignFn *PIC16F1TargetLowering::getCCAssignFn(bool IsVarArg) const {
  return IsVarArg ? CC_PIC16F1_Stack : CC_PIC16F1;
}

CCAssignFn *PIC16F1TargetLowering::getRetCCAssignFn() const {
  return RetCC_PIC16F1;
}

SDValue PIC16F1TargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();
  auto *FuncInfo = MF.getInfo<PIC16F1MachineFunctionInfo>();

  if (MF.getFunction().hasFnAttribute("interrupt") && !Ins.empty())
    report_fatal_error("an interrupt handler cannot take arguments");

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins,
                                isVarArg ? CC_PIC16F1_Stack : CC_PIC16F1);

  for (const CCValAssign &VA : ArgLocs) {
    EVT VT = VA.getLocVT();
    if (VA.isRegLoc()) {
      const TargetRegisterClass *RC = VT == MVT::i8 ? &PIC16F1::GPR8RegClass
                                                    : &PIC16F1::GPR16RegClass;
      Register VReg = RegInfo.createVirtualRegister(RC);
      RegInfo.addLiveIn(VA.getLocReg(), VReg);
      InVals.push_back(DAG.getCopyFromReg(Chain, dl, VReg, VT));
    } else {
      int FI = MFI.CreateFixedObject(VT.getStoreSize(), VA.getLocMemOffset(),
                                     true);
      SDValue FIN = DAG.getFrameIndex(FI, MVT::i16);
      InVals.push_back(DAG.getLoad(VT, dl, Chain, FIN,
                                   MachinePointerInfo::getFixedStack(MF, FI)));
    }
  }

  if (isVarArg)
    FuncInfo->setVarArgsFrameIndex(
        MFI.CreateFixedObject(1, CCInfo.getStackSize(), true));
  return Chain;
}

SDValue PIC16F1TargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                         SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc &dl = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins = CLI.Ins;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  CallingConv::ID CallConv = CLI.CallConv;
  bool isVarArg = CLI.IsVarArg;
  MachineFunction &MF = DAG.getMachineFunction();

  CLI.IsTailCall = false;

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeCallOperands(Outs, isVarArg ? CC_PIC16F1_Stack : CC_PIC16F1);
  unsigned NumBytes = CCInfo.getStackSize();

  Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, dl);

  SmallVector<std::pair<Register, SDValue>, 8> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;
  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    const CCValAssign &VA = ArgLocs[I];
    SDValue Arg = OutVals[I];
    if (Outs[I].Flags.isByVal())
      report_fatal_error("byval arguments are not supported; pass a pointer");
    if (VA.isRegLoc()) {
      RegsToPass.push_back({VA.getLocReg(), Arg});
    } else {
      // The outgoing-argument area is the bottom of this function's frame.
      MemOpChains.push_back(DAG.getNode(
          PIC16F1ISD::STORE_SP, dl, MVT::Other, Chain, Arg,
          DAG.getTargetConstant(VA.getLocMemOffset(), dl, MVT::i16)));
    }
  }
  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOpChains);

  SDValue InGlue;
  for (const auto &[Reg, Val] : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, dl, Reg, Val, InGlue);
    InGlue = Chain.getValue(1);
  }

  if (const auto *G = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), dl, MVT::i16);
  else if (const auto *E = dyn_cast<ExternalSymbolSDNode>(Callee))
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), MVT::i16);

  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);
  for (const auto &[Reg, Val] : RegsToPass)
    Ops.push_back(DAG.getRegister(Reg, Val.getValueType()));
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  Ops.push_back(DAG.getRegisterMask(TRI->getCallPreservedMask(MF, CallConv)));
  if (InGlue.getNode())
    Ops.push_back(InGlue);

  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  Chain = DAG.getNode(PIC16F1ISD::CALL, dl, NodeTys, Ops);
  InGlue = Chain.getValue(1);

  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, InGlue, dl);
  InGlue = Chain.getValue(1);

  SmallVector<CCValAssign, 16> RVLocs;
  CCState RetInfo(CallConv, isVarArg, MF, RVLocs, *DAG.getContext());
  RetInfo.AnalyzeCallResult(Ins, RetCC_PIC16F1);
  for (const CCValAssign &VA : RVLocs) {
    Chain = DAG.getCopyFromReg(Chain, dl, VA.getLocReg(), VA.getValVT(), InGlue)
                .getValue(1);
    InGlue = Chain.getValue(2);
    InVals.push_back(Chain.getValue(0));
  }
  return Chain;
}

bool PIC16F1TargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context,
    const Type *RetTy) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);
  return CCInfo.CheckReturn(Outs, RetCC_PIC16F1);
}

SDValue
PIC16F1TargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                   bool isVarArg,
                                   const SmallVectorImpl<ISD::OutputArg> &Outs,
                                   const SmallVectorImpl<SDValue> &OutVals,
                                   const SDLoc &dl, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  bool IsInterrupt = MF.getFunction().hasFnAttribute("interrupt");
  if (IsInterrupt && !Outs.empty())
    report_fatal_error("an interrupt handler cannot return a value");

  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, MF, RVLocs, *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_PIC16F1);

  SDValue Glue;
  SmallVector<SDValue, 4> RetOps(1, Chain);
  for (unsigned I = 0; I != RVLocs.size(); ++I) {
    const CCValAssign &VA = RVLocs[I];
    Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(), OutVals[I], Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }
  RetOps[0] = Chain;
  if (Glue.getNode())
    RetOps.push_back(Glue);
  return DAG.getNode(IsInterrupt ? PIC16F1ISD::RETI_GLUE : PIC16F1ISD::RET_GLUE,
                     dl, MVT::Other, RetOps);
}

SDValue PIC16F1TargetLowering::LowerVASTART(SDValue Op,
                                            SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  auto *FuncInfo = MF.getInfo<PIC16F1MachineFunctionInfo>();
  SDLoc dl(Op);
  SDValue FI = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(), MVT::i16);
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), dl, FI, Op.getOperand(1),
                      MachinePointerInfo(SV));
}

//===----------------------------------------------------------------------===//
// Addresses
//===----------------------------------------------------------------------===//

SDValue PIC16F1TargetLowering::LowerGlobalAddress(SDValue Op,
                                                  SelectionDAG &DAG) const {
  const auto *N = cast<GlobalAddressSDNode>(Op);
  const GlobalValue *GV = N->getGlobal();
  SDLoc dl(Op);
  // A function's address is its word address; data uses the FSR address.
  bool IsCode = isa<Function>(GV->getAliaseeObject() ? GV->getAliaseeObject()
                                                     : GV);
  SDValue Result = DAG.getTargetGlobalAddress(
      GV, dl, MVT::i16, N->getOffset(),
      IsCode ? PIC16F1II::MO_PM : PIC16F1II::MO_NONE);
  return DAG.getNode(PIC16F1ISD::WRAPPER, dl, MVT::i16, Result);
}

SDValue PIC16F1TargetLowering::LowerExternalSymbol(SDValue Op,
                                                   SelectionDAG &DAG) const {
  SDLoc dl(Op);
  const char *Sym = cast<ExternalSymbolSDNode>(Op)->getSymbol();
  SDValue Result = DAG.getTargetExternalSymbol(Sym, MVT::i16, PIC16F1II::MO_PM);
  return DAG.getNode(PIC16F1ISD::WRAPPER, dl, MVT::i16, Result);
}

SDValue PIC16F1TargetLowering::LowerBlockAddress(SDValue Op,
                                                 SelectionDAG &DAG) const {
  SDLoc dl(Op);
  const BlockAddress *BA = cast<BlockAddressSDNode>(Op)->getBlockAddress();
  SDValue Result =
      DAG.getTargetBlockAddress(BA, MVT::i16, 0, PIC16F1II::MO_PM);
  return DAG.getNode(PIC16F1ISD::WRAPPER, dl, MVT::i16, Result);
}

//===----------------------------------------------------------------------===//
// Shifts
//===----------------------------------------------------------------------===//

/// Custom handling of operations on types wider than a register pair. Leaving
/// Results empty selects the default expansion.
void PIC16F1TargetLowering::ReplaceNodeResults(SDNode *N,
                                               SmallVectorImpl<SDValue> &Results,
                                               SelectionDAG &DAG) const {
  unsigned Opc = N->getOpcode();
  if (Opc != ISD::SHL && Opc != ISD::SRL && Opc != ISD::SRA)
    return;
  EVT VT = N->getValueType(0);
  const auto *C = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!C || (VT != MVT::i32 && VT != MVT::i64))
    return;
  // Whole bytes are moves, and one or two bits are short enough inline.
  // Anything else costs a few instructions per bit for every byte of the
  // value, so call the shift routine, which loops.
  uint64_t Amt = C->getZExtValue();
  if (Amt % 8 == 0 || Amt < 3 || Amt >= VT.getSizeInBits())
    return;
  bool Is64 = VT == MVT::i64;
  RTLIB::Libcall LC =
      Opc == ISD::SHL   ? (Is64 ? RTLIB::SHL_I64 : RTLIB::SHL_I32)
      : Opc == ISD::SRL ? (Is64 ? RTLIB::SRL_I64 : RTLIB::SRL_I32)
                        : (Is64 ? RTLIB::SRA_I64 : RTLIB::SRA_I32);
  SDLoc dl(N);
  SDValue Ops[2] = {N->getOperand(0), DAG.getConstant(Amt, dl, MVT::i16)};
  MakeLibCallOptions CallOptions;
  Results.push_back(makeLibCall(DAG, LC, VT, Ops, CallOptions, dl).first);
}

SDValue PIC16F1TargetLowering::LowerShifts(SDValue Op,
                                           SelectionDAG &DAG) const {
  unsigned Opc = Op.getOpcode();
  EVT VT = Op.getValueType();
  SDLoc dl(Op);
  SDValue V = Op.getOperand(0);
  SDValue Amt = Op.getOperand(1);

  unsigned One, Var;
  switch (Opc) {
  case ISD::SHL:
    One = PIC16F1ISD::SHL1;
    Var = PIC16F1ISD::SHLV;
    break;
  case ISD::SRL:
    One = PIC16F1ISD::LSR1;
    Var = PIC16F1ISD::LSRV;
    break;
  default:
    One = PIC16F1ISD::ASR1;
    Var = PIC16F1ISD::ASRV;
    break;
  }

  const auto *C = dyn_cast<ConstantSDNode>(Amt);
  if (!C)
    return DAG.getNode(Var, dl, VT, V, DAG.getZExtOrTrunc(Amt, dl, MVT::i8));

  uint64_t N = C->getZExtValue();
  if (N >= VT.getSizeInBits())
    return DAG.getUNDEF(VT);

  // A 16-bit shift by eight or more is a byte move plus an 8-bit shift.
  if (VT == MVT::i16 && N >= 8) {
    unsigned Sub = Opc == ISD::SHL ? PIC16F1::sub_lo : PIC16F1::sub_hi;
    SDValue Byte = DAG.getTargetExtractSubreg(Sub, dl, MVT::i8, V);
    for (uint64_t I = 8; I != N; ++I)
      Byte = DAG.getNode(One, dl, MVT::i8, Byte);
    if (Opc == ISD::SHL)
      return DAG.getNode(PIC16F1ISD::MAKE16, dl, MVT::i16,
                         DAG.getConstant(0, dl, MVT::i8), Byte);
    return DAG.getNode(Opc == ISD::SRL ? ISD::ZERO_EXTEND : ISD::SIGN_EXTEND,
                       dl, MVT::i16, Byte);
  }

  for (uint64_t I = 0; I != N; ++I)
    V = DAG.getNode(One, dl, VT, V);
  return V;
}

//===----------------------------------------------------------------------===//
// Compare, branch and select
//===----------------------------------------------------------------------===//

/// Reduce an integer condition to one of the six the branch pseudos accept,
/// swapping the operands or adjusting a constant where needed.
static PIC16F1CC::CondCodes normalizeCondition(ISD::CondCode CC, SDValue &LHS,
                                               SDValue &RHS, const SDLoc &dl,
                                               SelectionDAG &DAG) {
  EVT VT = LHS.getValueType();
  unsigned Bits = VT.getSizeInBits();
  auto *C = dyn_cast<ConstantSDNode>(RHS);

  // x > C  is  x >= C+1, and x <= C  is  x < C+1, unless C+1 overflows.
  auto BumpConstant = [&](bool IsSigned) {
    if (!C)
      return false;
    APInt Val = C->getAPIntValue();
    if (IsSigned ? Val.isMaxSignedValue() : Val.isMaxValue())
      return false;
    RHS = DAG.getConstant(Val + 1, dl, VT);
    return true;
  };
  (void)Bits;

  switch (CC) {
  case ISD::SETEQ:
    return PIC16F1CC::COND_EQ;
  case ISD::SETNE:
    return PIC16F1CC::COND_NE;
  case ISD::SETULT:
    return PIC16F1CC::COND_ULT;
  case ISD::SETUGE:
    return PIC16F1CC::COND_UGE;
  case ISD::SETLT:
    return PIC16F1CC::COND_LT;
  case ISD::SETGE:
    return PIC16F1CC::COND_GE;
  case ISD::SETUGT:
    if (BumpConstant(false))
      return PIC16F1CC::COND_UGE;
    std::swap(LHS, RHS);
    return PIC16F1CC::COND_ULT;
  case ISD::SETULE:
    if (BumpConstant(false))
      return PIC16F1CC::COND_ULT;
    std::swap(LHS, RHS);
    return PIC16F1CC::COND_UGE;
  case ISD::SETGT:
    if (BumpConstant(true))
      return PIC16F1CC::COND_GE;
    std::swap(LHS, RHS);
    return PIC16F1CC::COND_LT;
  case ISD::SETLE:
    if (BumpConstant(true))
      return PIC16F1CC::COND_LT;
    std::swap(LHS, RHS);
    return PIC16F1CC::COND_GE;
  default:
    llvm_unreachable("unexpected integer condition");
  }
}

SDValue PIC16F1TargetLowering::LowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc dl(Op);
  PIC16F1CC::CondCodes PCC = normalizeCondition(CC, LHS, RHS, dl, DAG);
  return DAG.getNode(PIC16F1ISD::BR_CC, dl, MVT::Other, Chain,
                     DAG.getTargetConstant(PCC, dl, MVT::i8), LHS, RHS, Dest);
}

SDValue PIC16F1TargetLowering::LowerSELECT_CC(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDValue TrueV = Op.getOperand(2);
  SDValue FalseV = Op.getOperand(3);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  SDLoc dl(Op);
  PIC16F1CC::CondCodes PCC = normalizeCondition(CC, LHS, RHS, dl, DAG);
  return DAG.getNode(PIC16F1ISD::SELECT_CC, dl, Op.getValueType(),
                     DAG.getTargetConstant(PCC, dl, MVT::i8), LHS, RHS, TrueV,
                     FalseV);
}

//===----------------------------------------------------------------------===//
// Custom inserters
//===----------------------------------------------------------------------===//

MachineBasicBlock *
PIC16F1TargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                   MachineBasicBlock *BB) const {
  switch (MI.getOpcode()) {
  case PIC16F1::SELECT8c8:
  case PIC16F1::SELECT8c16:
  case PIC16F1::SELECT16c8:
  case PIC16F1::SELECT16c16:
    return emitSelect(MI, BB);
  case PIC16F1::SHLV8:
  case PIC16F1::LSRV8:
  case PIC16F1::ASRV8:
  case PIC16F1::SHLV16:
  case PIC16F1::LSRV16:
  case PIC16F1::ASRV16:
    return emitShiftLoop(MI, BB);
  default:
    llvm_unreachable("unexpected instruction for custom insertion");
  }
}

/// d = (a CC b) ? t : f  becomes a branch around a block that supplies f.
MachineBasicBlock *
PIC16F1TargetLowering::emitSelect(MachineInstr &MI,
                                  MachineBasicBlock *BB) const {
  const TargetInstrInfo &TII = *BB->getParent()->getSubtarget().getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();
  bool Cmp16 = MI.getOpcode() == PIC16F1::SELECT8c16 ||
               MI.getOpcode() == PIC16F1::SELECT16c16;

  MachineFunction *F = BB->getParent();
  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction::iterator It = ++BB->getIterator();
  MachineBasicBlock *FalseMBB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *SinkMBB = F->CreateMachineBasicBlock(LLVM_BB);
  F->insert(It, FalseMBB);
  F->insert(It, SinkMBB);

  SinkMBB->splice(SinkMBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  SinkMBB->transferSuccessorsAndUpdatePHIs(BB);
  BB->addSuccessor(FalseMBB);
  BB->addSuccessor(SinkMBB);

  BuildMI(BB, dl, TII.get(Cmp16 ? PIC16F1::BRCC16rr : PIC16F1::BRCC8rr))
      .addImm(MI.getOperand(1).getImm())
      .addReg(MI.getOperand(2).getReg())
      .addReg(MI.getOperand(3).getReg())
      .addMBB(SinkMBB);

  FalseMBB->addSuccessor(SinkMBB);

  BuildMI(*SinkMBB, SinkMBB->begin(), dl, TII.get(PIC16F1::PHI),
          MI.getOperand(0).getReg())
      .addReg(MI.getOperand(4).getReg())
      .addMBB(BB)
      .addReg(MI.getOperand(5).getReg())
      .addMBB(FalseMBB);

  MI.eraseFromParent();
  return SinkMBB;
}

/// A shift by a run-time amount: shift one bit per loop iteration.
MachineBasicBlock *
PIC16F1TargetLowering::emitShiftLoop(MachineInstr &MI,
                                     MachineBasicBlock *BB) const {
  MachineFunction *F = BB->getParent();
  MachineRegisterInfo &RI = F->getRegInfo();
  const TargetInstrInfo &TII = *F->getSubtarget().getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();

  unsigned StepOpc;
  const TargetRegisterClass *RC = &PIC16F1::GPR8RegClass;
  switch (MI.getOpcode()) {
  case PIC16F1::SHLV8:
    StepOpc = PIC16F1::SHL8;
    break;
  case PIC16F1::LSRV8:
    StepOpc = PIC16F1::LSR8;
    break;
  case PIC16F1::ASRV8:
    StepOpc = PIC16F1::ASR8;
    break;
  case PIC16F1::SHLV16:
    StepOpc = PIC16F1::SHL16;
    RC = &PIC16F1::GPR16RegClass;
    break;
  case PIC16F1::LSRV16:
    StepOpc = PIC16F1::LSR16;
    RC = &PIC16F1::GPR16RegClass;
    break;
  default:
    StepOpc = PIC16F1::ASR16;
    RC = &PIC16F1::GPR16RegClass;
    break;
  }

  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction::iterator It = ++BB->getIterator();
  MachineBasicBlock *LoopBB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *DoneBB = F->CreateMachineBasicBlock(LLVM_BB);
  F->insert(It, LoopBB);
  F->insert(It, DoneBB);

  DoneBB->splice(DoneBB->begin(), BB,
                 std::next(MachineBasicBlock::iterator(MI)), BB->end());
  DoneBB->transferSuccessorsAndUpdatePHIs(BB);
  BB->addSuccessor(LoopBB);
  BB->addSuccessor(DoneBB);
  LoopBB->addSuccessor(DoneBB);
  LoopBB->addSuccessor(LoopBB);

  Register DstReg = MI.getOperand(0).getReg();
  Register SrcReg = MI.getOperand(1).getReg();
  Register AmtReg = MI.getOperand(2).getReg();
  Register ValPhi = RI.createVirtualRegister(RC);
  Register ValNext = RI.createVirtualRegister(RC);
  Register AmtPhi = RI.createVirtualRegister(&PIC16F1::GPR8RegClass);
  Register AmtNext = RI.createVirtualRegister(&PIC16F1::GPR8RegClass);

  // Skip the loop when the amount is zero.
  BuildMI(BB, dl, TII.get(PIC16F1::BRCC8ri))
      .addImm(PIC16F1CC::COND_EQ)
      .addReg(AmtReg)
      .addImm(0)
      .addMBB(DoneBB);

  BuildMI(LoopBB, dl, TII.get(PIC16F1::PHI), ValPhi)
      .addReg(SrcReg)
      .addMBB(BB)
      .addReg(ValNext)
      .addMBB(LoopBB);
  BuildMI(LoopBB, dl, TII.get(PIC16F1::PHI), AmtPhi)
      .addReg(AmtReg)
      .addMBB(BB)
      .addReg(AmtNext)
      .addMBB(LoopBB);
  BuildMI(LoopBB, dl, TII.get(StepOpc), ValNext).addReg(ValPhi);
  BuildMI(LoopBB, dl, TII.get(PIC16F1::ADD8ri), AmtNext)
      .addReg(AmtPhi)
      .addImm(0xFF);
  BuildMI(LoopBB, dl, TII.get(PIC16F1::BRCC8ri))
      .addImm(PIC16F1CC::COND_NE)
      .addReg(AmtNext)
      .addImm(0)
      .addMBB(LoopBB);

  BuildMI(*DoneBB, DoneBB->begin(), dl, TII.get(PIC16F1::PHI), DstReg)
      .addReg(SrcReg)
      .addMBB(BB)
      .addReg(ValNext)
      .addMBB(LoopBB);

  MI.eraseFromParent();
  return DoneBB;
}
