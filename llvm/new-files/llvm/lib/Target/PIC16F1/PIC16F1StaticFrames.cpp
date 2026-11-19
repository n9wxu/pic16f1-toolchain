//===-- PIC16F1StaticFrames.cpp - Fixed addresses for locals --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A function that can never be entered while it is already running needs no
// stack frame: its variables can have fixed addresses, where they are
// reached with direct addressing and changed in place. This pass
//
//  1. finds those functions and marks them "pic16f1-static-frame", and
//  2. gives the small variables they keep in memory a fixed address each.
//
// A function qualifies if
//  - nothing outside this module can reach it: neither it nor any of its
//    callers has external linkage or has its address taken. (Code in
//    another file may run in an interrupt handler, where it could get to
//    the function while the main program is inside it.) main is the
//    exception: only the startup code calls it, unless
//    -pic16f1-main-saves-registers says that another file does;
//  - it cannot call itself, directly or through others;
//  - it cannot run in an interrupt handler of this module.
//
// With -flto the whole program is one module and nearly every function
// qualifies. This is the first stage of static frames: spill slots and saved
// registers are still on the software stack, and the fixed variables of
// different functions do not yet share memory.
//
//===----------------------------------------------------------------------===//

#include "PIC16F1.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

#define DEBUG_TYPE "pic16f1-static-frames"

static cl::opt<bool> EnableStaticFrames(
    "pic16f1-static-frames", cl::Hidden, cl::init(true),
    cl::desc("Give the variables of functions that cannot be re-entered "
             "fixed addresses"));

static cl::opt<unsigned> StaticLocalLimit(
    "pic16f1-static-local-limit", cl::Hidden, cl::init(16),
    cl::desc("The largest variable, in bytes, that gets a fixed address. A "
             "variable of up to 16 bytes is kept in one bank by the linker"));

namespace {

class PIC16F1StaticFrames : public ModulePass {
public:
  static char ID;
  PIC16F1StaticFrames() : ModulePass(ID) {}
  StringRef getPassName() const override { return "PIC16F1 static frames"; }
  bool runOnModule(Module &M) override;

private:
  bool fixLocals(Function &F);
};

} // end anonymous namespace

char PIC16F1StaticFrames::ID = 0;

INITIALIZE_PASS(PIC16F1StaticFrames, DEBUG_TYPE, "PIC16F1 static frames",
                false, false)

ModulePass *llvm::createPIC16F1StaticFramesPass() {
  return new PIC16F1StaticFrames();
}

bool PIC16F1StaticFrames::runOnModule(Module &M) {
  if (!EnableStaticFrames)
    return false;

  // The call graph, with one extra node (nullptr) for everything outside
  // the module.
  using Node = const Function *;
  const Node Outside = nullptr;
  DenseMap<Node, SmallPtrSet<Node, 8>> Calls;
  auto CalledFromOutside = [](const Function &F) {
    if (F.isDeclaration())
      return false;
    if (F.getName() == "main" && !pic16f1MainIsCalled())
      return F.hasAddressTaken();
    return !F.hasLocalLinkage() || F.hasAddressTaken();
  };
  for (const Function &F : M) {
    if (F.isDeclaration())
      continue;
    if (CalledFromOutside(F))
      Calls[Outside].insert(&F);
    auto &Out = Calls[&F];
    for (const Instruction &I : instructions(F)) {
      const auto *CB = dyn_cast<CallBase>(&I);
      if (!CB || isa<IntrinsicInst>(CB))
        continue;
      const Function *Callee = CB->getCalledFunction();
      // Inline assembly could call anything.
      if (!Callee || Callee->isDeclaration())
        Out.insert(Outside);
      else
        Out.insert(Callee);
    }
  }

  // Everything each node can reach in one or more calls.
  auto Reachable = [&](Node From) {
    SmallPtrSet<Node, 16> Seen;
    SmallVector<Node, 16> Work;
    auto Push = [&](Node N) {
      for (Node Next : Calls.lookup(N))
        if (Seen.insert(Next).second)
          Work.push_back(Next);
    };
    Push(From);
    while (!Work.empty())
      Push(Work.pop_back_val());
    return Seen;
  };

  SmallPtrSet<Node, 16> InInterrupt;
  for (const Function &F : M) {
    if (F.isDeclaration() || !F.hasFnAttribute("interrupt"))
      continue;
    InInterrupt.insert(&F);
    for (Node N : Reachable(&F))
      InInterrupt.insert(N);
  }
  const SmallPtrSet<Node, 16> FromOutside = Reachable(Outside);

  bool Changed = false;
  for (Function &F : M) {
    if (F.isDeclaration() || F.hasFnAttribute(Attribute::Naked) ||
        F.hasFnAttribute(Attribute::OptimizeNone))
      continue;
    // Whatever the outside can reach, an interrupt handler in another file
    // can reach too. (main is entered from outside once, by the startup
    // code; that is not counted.)
    if (CalledFromOutside(F) || FromOutside.count(&F) ||
        InInterrupt.count(&F) || Reachable(&F).count(&F))
      continue;
    F.addFnAttr("pic16f1-static-frame");
    Changed = true;
    fixLocals(F);
  }
  return Changed;
}

/// Give each small variable of \p F that lives in memory a fixed address.
bool PIC16F1StaticFrames::fixLocals(Function &F) {
  Module &M = *F.getParent();
  const DataLayout &DL = M.getDataLayout();
  SmallVector<AllocaInst *, 8> Locals;
  for (Instruction &I : F.getEntryBlock())
    if (auto *AI = dyn_cast<AllocaInst>(&I))
      if (AI->isStaticAlloca() && !AI->isUsedWithInAlloca() &&
          !AI->isSwiftError())
        if (auto Size = AI->getAllocationSize(DL))
          if (!Size->isScalable() && Size->getFixedValue() != 0 &&
              Size->getFixedValue() <= StaticLocalLimit)
            Locals.push_back(AI);
  // Pack them into blocks of up to 16 bytes. The linker keeps a block
  // that small within one bank, so one bank selection serves all the
  // variables in it. Like any local, they have no value until the function
  // gives them one, so they are not cleared at startup.
  const uint64_t BlockLimit = 16;
  struct Block {
    uint64_t Size = 0;
    SmallVector<std::pair<AllocaInst *, uint64_t>, 4> Members;
  };
  SmallVector<Block, 2> Blocks;
  for (AllocaInst *AI : Locals) {
    uint64_t Size = AI->getAllocationSize(DL)->getFixedValue();
    uint64_t Align = AI->getAlign().value();
    Block *Into = nullptr;
    uint64_t At = 0;
    for (Block &B : Blocks) {
      At = alignTo(B.Size, Align);
      if (At + Size <= BlockLimit) {
        Into = &B;
        break;
      }
    }
    if (!Into) {
      Into = &Blocks.emplace_back();
      At = 0;
    }
    Into->Members.push_back({AI, At});
    Into->Size = At + Size;
  }
  Type *I8 = Type::getInt8Ty(M.getContext());
  for (Block &B : Blocks) {
    Type *Ty = ArrayType::get(I8, B.Size);
    Align MaxAlign(1);
    for (auto &[AI, At] : B.Members)
      MaxAlign = std::max(MaxAlign, AI->getAlign());
    auto *GV = new GlobalVariable(
        M, Ty, /*isConstant=*/false, GlobalValue::InternalLinkage,
        Constant::getNullValue(Ty), F.getName() + ".frame", nullptr,
        GlobalValue::NotThreadLocal, B.Members[0].first->getAddressSpace());
    GV->setAlignment(MaxAlign);
    GV->setSection((".noinit." + GV->getName()).str());
    for (auto &[AI, At] : B.Members) {
      // The markers of where a stack variable's life begins and ends mean
      // nothing for one with a fixed address.
      SmallVector<Instruction *, 4> Dead;
      for (User *U : AI->users())
        if (auto *II = dyn_cast<LifetimeIntrinsic>(U))
          Dead.push_back(II);
      for (Instruction *I : Dead)
        I->eraseFromParent();
      Constant *Addr = ConstantExpr::getInBoundsGetElementPtr(
          I8, GV, ConstantInt::get(Type::getInt16Ty(M.getContext()), At));
      AI->replaceAllUsesWith(Addr);
      AI->eraseFromParent();
    }
  }
  return !Locals.empty();
}
