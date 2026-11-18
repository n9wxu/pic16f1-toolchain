//===-- PIC16F1ISelDAGToDAG.cpp - Instruction selection -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PIC16F1.h"
#include "PIC16F1SelectionDAGInfo.h"
#include "PIC16F1TargetMachine.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "pic16f1-isel"
#define PASS_NAME "PIC16F1 DAG->DAG Pattern Instruction Selection"

namespace {

class PIC16F1DAGToDAGISel : public SelectionDAGISel {
public:
  PIC16F1DAGToDAGISel() = delete;
  PIC16F1DAGToDAGISel(PIC16F1TargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISel(TM, OptLevel) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    Subtarget = &MF.getSubtarget<PIC16F1Subtarget>();
    return SelectionDAGISel::runOnMachineFunction(MF);
  }

private:
  const PIC16F1Subtarget *Subtarget = nullptr;

#include "PIC16F1GenDAGISel.inc"

  void Select(SDNode *N) override;

  /// A memory operand of inline assembly is passed as its address.
  bool SelectInlineAsmMemoryOperand(const SDValue &Op,
                                    InlineAsm::ConstraintCode ConstraintID,
                                    std::vector<SDValue> &OutOps) override {
    OutOps.push_back(Op);
    return false;
  }

  bool SelectAbs(SDValue Addr, SDValue &Out);
  bool SelectPtr(SDValue Addr, SDValue &Base, SDValue &Off, int64_t MaxOff);
  bool SelectPtr8(SDValue Addr, SDValue &Base, SDValue &Off) {
    return SelectPtr(Addr, Base, Off, 31);
  }
  bool SelectPtr16(SDValue Addr, SDValue &Base, SDValue &Off) {
    return SelectPtr(Addr, Base, Off, 30);
  }
  bool SelectFI(SDValue Addr, SDValue &FI, SDValue &Off);
};

class PIC16F1DAGToDAGISelLegacy : public SelectionDAGISelLegacy {
public:
  static char ID;
  PIC16F1DAGToDAGISelLegacy(PIC16F1TargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISelLegacy(
            ID, std::make_unique<PIC16F1DAGToDAGISel>(TM, OptLevel)) {}
};

} // end anonymous namespace

char PIC16F1DAGToDAGISelLegacy::ID;

INITIALIZE_PASS(PIC16F1DAGToDAGISelLegacy, DEBUG_TYPE, PASS_NAME, false, false)

FunctionPass *llvm::createPIC16F1ISelDag(PIC16F1TargetMachine &TM,
                                         CodeGenOptLevel OptLevel) {
  return new PIC16F1DAGToDAGISelLegacy(TM, OptLevel);
}

/// Data that banked (direct) addressing can reach: a writable global, which
/// the linker places in RAM. Constant globals live in flash and are read
/// through a pointer.
static bool isDirectlyAddressable(const GlobalValue *GV) {
  const auto *Var = dyn_cast<GlobalVariable>(GV);
  return Var && !Var->isConstant();
}

bool PIC16F1DAGToDAGISel::SelectAbs(SDValue Addr, SDValue &Out) {
  SDLoc dl(Addr);
  // A constant banked address, such as a special function register.
  if (const auto *C = dyn_cast<ConstantSDNode>(Addr)) {
    uint64_t V = C->getZExtValue();
    if (V >= MF->getSubtarget<PIC16F1Subtarget>().bankedEnd())
      return false;
    Out = CurDAG->getTargetConstant(V, dl, MVT::i16);
    return true;
  }
  int64_t Extra = 0;
  if (Addr.getOpcode() == ISD::ADD) {
    const auto *C = dyn_cast<ConstantSDNode>(Addr.getOperand(1));
    if (!C)
      return false;
    Extra = C->getSExtValue();
    Addr = Addr.getOperand(0);
  }
  if (Addr.getOpcode() != PIC16F1ISD::WRAPPER)
    return false;
  const auto *G = dyn_cast<GlobalAddressSDNode>(Addr.getOperand(0));
  if (!G || !isDirectlyAddressable(G->getGlobal()))
    return false;
  Out = CurDAG->getTargetGlobalAddress(G->getGlobal(), dl, MVT::i16,
                                       G->getOffset() + Extra);
  return true;
}

bool PIC16F1DAGToDAGISel::SelectPtr(SDValue Addr, SDValue &Base, SDValue &Off,
                                    int64_t MaxOff) {
  SDLoc dl(Addr);
  if (CurDAG->isBaseWithConstantOffset(Addr)) {
    int64_t C = cast<ConstantSDNode>(Addr.getOperand(1))->getSExtValue();
    if (C >= -32 && C <= MaxOff &&
        Addr.getOperand(0).getOpcode() != ISD::FrameIndex) {
      Base = Addr.getOperand(0);
      Off = CurDAG->getSignedTargetConstant(C, dl, MVT::i8);
      return true;
    }
  }
  Base = Addr;
  Off = CurDAG->getTargetConstant(0, dl, MVT::i8);
  return true;
}

bool PIC16F1DAGToDAGISel::SelectFI(SDValue Addr, SDValue &FI, SDValue &Off) {
  SDLoc dl(Addr);
  if (const auto *F = dyn_cast<FrameIndexSDNode>(Addr)) {
    FI = CurDAG->getTargetFrameIndex(F->getIndex(), MVT::i16);
    Off = CurDAG->getTargetConstant(0, dl, MVT::i16);
    return true;
  }
  if (CurDAG->isBaseWithConstantOffset(Addr))
    if (const auto *F = dyn_cast<FrameIndexSDNode>(Addr.getOperand(0))) {
      int64_t C = cast<ConstantSDNode>(Addr.getOperand(1))->getSExtValue();
      if (C < 0)
        return false;
      FI = CurDAG->getTargetFrameIndex(F->getIndex(), MVT::i16);
      Off = CurDAG->getTargetConstant(C, dl, MVT::i16);
      return true;
    }
  return false;
}

void PIC16F1DAGToDAGISel::Select(SDNode *Node) {
  SDLoc dl(Node);
  if (Node->isMachineOpcode()) {
    Node->setNodeId(-1);
    return;
  }
  if (Node->getOpcode() == ISD::FrameIndex) {
    int FI = cast<FrameIndexSDNode>(Node)->getIndex();
    SDValue TFI = CurDAG->getTargetFrameIndex(FI, MVT::i16);
    ReplaceNode(Node, CurDAG->getMachineNode(
                          PIC16F1::LEAfi, dl, MVT::i16, TFI,
                          CurDAG->getTargetConstant(0, dl, MVT::i16)));
    return;
  }
  SelectCode(Node);
}
