//===-- PIC16F1PointerIVs.cpp - Step pointers through arrays --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// a[i] in a loop, with i a counter, is the address of a plus i: on this
// processor a 16-bit addition into an FSR for every access. A pointer that
// starts at a and is stepped each round costs one instruction for the access
// and the step together (MOVIW FSR0++).
//
// LLVM's loop strength reduction would make that change, but the counter is
// usually a byte, widened to 16 bits for the address, and it does not look
// through the widening. This pass does the one case: an address made of a
// value fixed for the loop plus a constant multiple of a loop counter that
// cannot wrap. It runs before loop strength reduction, which then sees a
// counter that only counts and turns it to count down to zero.
//
// There are two pointer registers at most (FSR0, and FSR1 in a function
// with a static frame). A loop that needs more addresses than that is left
// alone, since a pointer that has to be copied into its FSR for each access
// is no better than an index; so is a loop that makes calls.
//
//===----------------------------------------------------------------------===//

#include "PIC16F1.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/Local.h"

using namespace llvm;

#define DEBUG_TYPE "pic16f1-pointer-ivs"

static cl::opt<bool> EnablePointerIVs(
    "pic16f1-pointer-ivs", cl::Hidden, cl::init(true),
    cl::desc("Step a pointer through an array instead of indexing it"));

namespace {

class PIC16F1PointerIVs : public FunctionPass {
public:
  static char ID;
  PIC16F1PointerIVs() : FunctionPass(ID) {}
  StringRef getPassName() const override { return "PIC16F1 pointer steps"; }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addPreserved<LoopInfoWrapperPass>();
    // For a block in front of each loop to start the pointer in.
    AU.addRequiredID(LoopSimplifyID);
    AU.addPreservedID(LoopSimplifyID);
    AU.setPreservesCFG();
  }
  bool runOnFunction(Function &F) override;

private:
  bool runOnLoop(Loop &L, const DataLayout &DL, unsigned Pointers);
};

/// The accesses of one loop that one stepping pointer serves: addresses
/// Base + Scale * counter + a constant, the constants close together.
struct Group {
  Value *Base;
  PHINode *Counter;
  int64_t Scale;
  int64_t FirstOffset;
  SmallVector<std::pair<GetElementPtrInst *, int64_t>, 4> Addresses;
  unsigned Accesses = 0;
};

} // end anonymous namespace

char PIC16F1PointerIVs::ID = 0;

INITIALIZE_PASS_BEGIN(PIC16F1PointerIVs, DEBUG_TYPE, "PIC16F1 pointer steps",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopSimplify)
INITIALIZE_PASS_END(PIC16F1PointerIVs, DEBUG_TYPE, "PIC16F1 pointer steps",
                    false, false)

FunctionPass *llvm::createPIC16F1PointerIVsPass() {
  return new PIC16F1PointerIVs();
}

bool PIC16F1PointerIVs::runOnFunction(Function &F) {
  if (!EnablePointerIVs || skipFunction(F))
    return false;
  LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  const DataLayout &DL = F.getDataLayout();
  unsigned Pointers = F.hasFnAttribute("pic16f1-static-frame") ? 2 : 1;
  bool Changed = false;
  for (Loop *L : LI.getLoopsInPreorder())
    Changed |= runOnLoop(*L, DL, Pointers);
  return Changed;
}

/// If \p V is a counter of \p L (or one widened without loss), give the
/// counter and its constant step.
static PHINode *asCounter(Value *V, const Loop &L, int64_t &Step) {
  bool Widened = false;
  if (auto *Z = dyn_cast<ZExtInst>(V)) {
    V = Z->getOperand(0);
    Widened = true;
  }
  auto *Phi = dyn_cast<PHINode>(V);
  BasicBlock *Latch = L.getLoopLatch();
  if (!Phi || Phi->getParent() != L.getHeader() ||
      Phi->getNumIncomingValues() != 2 || Phi->getBasicBlockIndex(Latch) < 0)
    return nullptr;
  auto *Next =
      dyn_cast<BinaryOperator>(Phi->getIncomingValueForBlock(Latch));
  if (!Next || Next->getOpcode() != Instruction::Add)
    return nullptr;
  Value *Other = Next->getOperand(0) == Phi   ? Next->getOperand(1)
                 : Next->getOperand(1) == Phi ? Next->getOperand(0)
                                              : nullptr;
  auto *C = dyn_cast_or_null<ConstantInt>(Other);
  if (!C)
    return nullptr;
  // Widening commutes with the step only if the narrow count cannot wrap.
  if (Widened && (!Next->hasNoUnsignedWrap() || C->isNegative()))
    return nullptr;
  Step = C->getSExtValue();
  return Phi;
}

bool PIC16F1PointerIVs::runOnLoop(Loop &L, const DataLayout &DL,
                                  unsigned Pointers) {
  BasicBlock *Preheader = L.getLoopPreheader();
  BasicBlock *Latch = L.getLoopLatch();
  BasicBlock *Header = L.getHeader();
  if (!Preheader || !Latch || pred_size(Header) != 2)
    return false;

  SmallVector<Group, 4> Groups;
  for (BasicBlock *BB : L.blocks()) {
    for (Instruction &I : *BB) {
      // A call clobbers FSR0 and needs the stack pointer in FSR1.
      if (auto *CB = dyn_cast<CallBase>(&I))
        if (!isa<IntrinsicInst>(CB) || isa<MemIntrinsic>(CB))
          return false;
      auto *GEP = dyn_cast<GetElementPtrInst>(&I);
      if (!GEP || !L.isLoopInvariant(GEP->getPointerOperand()))
        continue;
      unsigned Bits = DL.getIndexTypeSizeInBits(GEP->getType());
      SmallMapVector<Value *, APInt, 4> Variable;
      APInt Constant(Bits, 0);
      if (!GEP->collectOffset(DL, Bits, Variable, Constant) ||
          Variable.size() != 1)
        continue;
      int64_t Step;
      PHINode *Counter = asCounter(Variable.begin()->first, L, Step);
      if (!Counter)
        continue;
      int64_t Scale = Variable.begin()->second.getSExtValue();
      int64_t Stride = Scale * Step;
      // What ADDFSR can add.
      if (Stride == 0 || Stride < -32 || Stride > 31)
        continue;
      // Every use must be in the loop. The loads and stores through the
      // address are counted further down.
      bool Inside = true;
      for (User *U : GEP->users())
        Inside &= L.contains(cast<Instruction>(U));
      if (!Inside)
        continue;
      int64_t Offset = Constant.getSExtValue();
      Group *Into = nullptr;
      for (Group &G : Groups)
        if (G.Base == GEP->getPointerOperand() && G.Counter == Counter &&
            G.Scale == Scale && Offset - G.FirstOffset >= -32 &&
            Offset - G.FirstOffset <= 30) {
          Into = &G;
          break;
        }
      if (!Into) {
        Groups.push_back({GEP->getPointerOperand(), Counter, Scale, Offset, {}});
        Into = &Groups.back();
      }
      Into->Addresses.push_back({GEP, Offset - Into->FirstOffset});
    }
  }
  if (Groups.empty())
    return false;
  // A pointer only stays in its FSR if nothing else in the loop needs one:
  // count the other addresses that are not constants.
  SmallPtrSet<const Value *, 8> Ours, Used, Others;
  for (const Group &G : Groups)
    for (auto &[GEP, Delta] : G.Addresses)
      Ours.insert(GEP);
  for (BasicBlock *BB : L.blocks())
    for (Instruction &I : *BB) {
      const Value *Ptr = nullptr;
      if (auto *LD = dyn_cast<LoadInst>(&I))
        Ptr = LD->getPointerOperand();
      else if (auto *ST = dyn_cast<StoreInst>(&I))
        Ptr = ST->getPointerOperand();
      if (!Ptr)
        continue;
      // Through any constant offsets on top.
      APInt Off(DL.getIndexTypeSizeInBits(Ptr->getType()), 0);
      Ptr = Ptr->stripAndAccumulateConstantOffsets(DL, Off,
                                                   /*AllowNonInbounds=*/true);
      if (Ours.count(Ptr))
        Used.insert(Ptr);
      else if (!isa<Constant>(Ptr) && !isa<AllocaInst>(Ptr))
        Others.insert(Ptr);
    }
  // A group none of whose addresses is loaded or stored through gains
  // nothing.
  llvm::erase_if(Groups, [&](const Group &G) {
    return llvm::none_of(G.Addresses,
                         [&](const auto &A) { return Used.count(A.first); });
  });
  if (Groups.empty())
    return false;
  if (Groups.size() + Others.size() > Pointers)
    return false;

  LLVMContext &Ctx = Header->getContext();
  Type *I8 = Type::getInt8Ty(Ctx);
  for (Group &G : Groups) {
    Type *IdxTy = DL.getIndexType(G.Base->getType());
    int64_t Step;
    asCounter(G.Counter, L, Step);
    // The pointer's first value: the address for the counter's first.
    IRBuilder<> Pre(Preheader->getTerminator());
    Value *Start = Pre.CreateZExtOrTrunc(
        G.Counter->getIncomingValueForBlock(Preheader), IdxTy);
    Value *First = Pre.CreateAdd(
        Pre.CreateMul(Start, ConstantInt::get(IdxTy, G.Scale, true)),
        ConstantInt::get(IdxTy, G.FirstOffset, true));
    Value *P0 = Pre.CreateGEP(I8, G.Base, First, "step.start");
    PHINode *P = PHINode::Create(G.Base->getType(), 2, "step",
                                 Header->begin());
    IRBuilder<> AtLatch(Latch->getTerminator());
    Value *Next = AtLatch.CreateGEP(
        I8, P, ConstantInt::get(IdxTy, G.Scale * Step, true), "step.next");
    P->addIncoming(P0, Preheader);
    P->addIncoming(Next, Latch);
    for (auto &[GEP, Delta] : G.Addresses) {
      Value *At = P;
      if (Delta != 0) {
        IRBuilder<> B(GEP);
        At = B.CreateGEP(I8, P, ConstantInt::get(IdxTy, Delta, true));
      }
      GEP->replaceAllUsesWith(At);
      // With it goes the widened counter, if that was its only use: the
      // counter must be seen to do nothing but count.
      RecursivelyDeleteTriviallyDeadInstructions(GEP);
    }
  }
  return true;
}
