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
// A function this module cannot decide about (one that other files can
// call, or anything reached from one) is compiled twice: as F, with its frame
// on the stack, and as F.sf, with a static frame. The linker knows every
// call in the program and uses F.sf where F cannot be re-entered.
//
// With -flto the whole program is one module and nearly every function
// qualifies outright. The linker then places the frames, letting those of
// functions that are never active together share memory (lld's
// Arch/PIC16F1.cpp).
//
//===----------------------------------------------------------------------===//

#include "PIC16F1.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugProgramInstruction.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

using namespace llvm;

#define DEBUG_TYPE "pic16f1-static-frames"

static cl::opt<bool> EnableStaticFrames(
    "pic16f1-static-frames", cl::Hidden, cl::init(true),
    cl::desc("Give the variables of functions that cannot be re-entered "
             "fixed addresses"));

static cl::opt<bool> DualFrames(
    "pic16f1-dual-frames", cl::Hidden, cl::init(true),
    cl::desc("Compile a function that might or might not be re-entered both "
             "ways, with a stack frame and with a static frame, for the "
             "linker to choose between"));

static cl::opt<unsigned> StaticLocalLimit(
    "pic16f1-static-local-limit", cl::Hidden, cl::init(16),
    cl::desc("The largest variable, in bytes, that gets a fixed address. A "
             "variable of up to 16 bytes is kept in one bank by the linker"));

/// A function with a static frame never touches the software stack: it is
/// compiled without a stack pointer. So it must not need one. This says
/// whether \p F might: for arguments it takes or passes on the stack, or for
/// a variable too large to be given a fixed address while fixed variables
/// cannot yet share memory. The checks on calls are deliberately wide; the
/// call lowering stops the compilation if one slips through.
static bool mayNeedStack(const Function &F) {
  const DataLayout &DL = F.getDataLayout();
  auto TooWide = [](Type *Ty) {
    return Ty->isIntegerTy() && Ty->getIntegerBitWidth() > 32;
  };
  // Arguments go in R0-R7, eight bytes.
  auto OnStack = [&](FunctionType *Ty, AttributeList Attrs) {
    if (Ty->isVarArg())
      return true;
    uint64_t Bytes = 0;
    for (unsigned I = 0, E = Ty->getNumParams(); I != E; ++I) {
      if (Attrs.hasParamAttr(I, Attribute::ByVal))
        return true;
      Bytes += DL.getTypeAllocSize(Ty->getParamType(I));
    }
    return Bytes > 8;
  };
  if (OnStack(F.getFunctionType(), F.getAttributes()))
    return true;
  for (const BasicBlock &BB : F) {
    for (const Instruction &I : BB) {
      if (const auto *AI = dyn_cast<AllocaInst>(&I)) {
        auto Size = AI->getAllocationSize(DL);
        if (!AI->isStaticAlloca() || !Size || Size->isScalable() ||
            Size->getFixedValue() > pic16f1StaticSlotLimit())
          return true;
      }
      // 64-bit arithmetic calls library routines with stack arguments.
      if (TooWide(I.getType()))
        return true;
      for (const Use &Op : I.operands())
        if (TooWide(Op->getType()))
          return true;
      const auto *CB = dyn_cast<CallBase>(&I);
      if (!CB)
        continue;
      if (CB->isInlineAsm())
        return true;
      if (const auto *II = dyn_cast<IntrinsicInst>(CB)) {
        // A 32-bit multiply with overflow goes through the 64-bit multiply.
        Intrinsic::ID ID = II->getIntrinsicID();
        if (ID == Intrinsic::smul_with_overflow ||
            ID == Intrinsic::umul_with_overflow || ID == Intrinsic::vastart)
          return true;
        continue;
      }
      uint64_t Bytes = 0;
      for (const Use &Arg : CB->args())
        Bytes += DL.getTypeAllocSize(Arg->getType());
      if (CB->getFunctionType()->isVarArg() || Bytes > 8)
        return true;
    }
  }
  return false;
}

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

  // The same, staying inside the module: what a function can reach without
  // any help from outside.
  auto ReachableInside = [&](Node From) {
    SmallPtrSet<Node, 16> Seen;
    SmallVector<Node, 16> Work;
    auto Push = [&](Node N) {
      for (Node Next : Calls.lookup(N))
        if (Next != Outside && Seen.insert(Next).second)
          Work.push_back(Next);
    };
    Push(From);
    while (!Work.empty())
      Push(Work.pop_back_val());
    return Seen;
  };

  bool Changed = false;
  SmallVector<Function *, 16> Undecided;
  for (Function &F : M) {
    if (F.isDeclaration() || F.hasFnAttribute(Attribute::Naked) ||
        F.hasFnAttribute(Attribute::OptimizeNone) || mayNeedStack(F))
      continue;
    // What this module cannot settle, the linker can: it sees every call
    // in the program. Such a function is compiled both ways. Not one that
    // calls itself within the module, which settles it, and not an
    // interrupt handler.
    if ((CalledFromOutside(F) || FromOutside.count(&F) ||
         InInterrupt.count(&F)) &&
        DualFrames && !F.hasFnAttribute("interrupt") &&
        !ReachableInside(&F).count(&F) &&
        // The address of a label belongs to one copy of the code.
        llvm::none_of(F, [](const BasicBlock &BB) {
          return BB.hasAddressTaken();
        }))
      Undecided.push_back(&F);
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

  // F stays as it is, with its frame on the stack, and every call goes to
  // it. F.sf is the same function with a static frame. The linker points F
  // at F.sf if it finds that F cannot be re-entered (lld, PIC16F1Frames).
  // Callers are compiled against F and may know exactly which registers it
  // changes, so F.sf is held to changing no others; see getCalleeSavedRegs.
  SmallVector<GlobalValue *, 16> Versions;
  //
  // F.isr is a second static version, with variables of its own, for use in
  // an interrupt handler: a function that both the main program and a
  // handler call can then have static frames after all, at the price of
  // being in the program twice. Code that runs in a handler calls G$isr
  // where it means G; the linker makes G$isr the interrupt copy of G if it
  // duplicates G, and G itself otherwise. The handlers themselves do too.
  std::string Suffix;
  {
    MD5 Hash;
    Hash.update(M.getSourceFileName());
    Hash.update(M.getName());
    MD5::MD5Result Result;
    Hash.final(Result);
    Suffix = ("." + Twine::utohexstr(Result.low() & 0xFFFFFFFF)).str();
  }
  SmallPtrSet<GlobalValue *, 8> Kept;
  auto CallInterruptCopies = [&](Function &Code) {
    for (Instruction &I : instructions(Code)) {
      auto *CB = dyn_cast<CallBase>(&I);
      if (!CB || isa<IntrinsicInst>(CB))
        continue;
      Function *Callee = CB->getCalledFunction();
      if (!Callee || Callee->hasFnAttribute("pic16f1-isr-of") ||
          Callee->getFunctionType() != CB->getFunctionType())
        continue;
      // A name of its own for a function only this file can see.
      std::string Name = (Callee->getName() + "$isr" +
                          (Callee->hasLocalLinkage() ? Suffix : ""))
                             .str();
      Function *Copy = M.getFunction(Name);
      if (!Copy) {
        Copy = Function::Create(Callee->getFunctionType(),
                                GlobalValue::ExternalLinkage,
                                Callee->getAddressSpace(), Name, &M);
        Copy->setCallingConv(Callee->getCallingConv());
        Copy->setAttributes(Callee->getAttributes());
        for (const char *Ours :
             {"pic16f1-dual", "pic16f1-dual-of", "pic16f1-dual-sf",
              "pic16f1-static-frame", "interrupt"})
          Copy->removeFnAttr(Ours);
        Copy->addFnAttr("pic16f1-isr-of", Callee->getName());
        // A function only this file calls may not keep to the calling
        // convention (its callers know what it changes), and its interrupt
        // copy's callers know nothing: see PIC16F1CallLowering.cpp.
        if (Callee->hasLocalLinkage())
          Copy->addFnAttr("pic16f1-isr-of-local");
        // The function itself may be left without a caller in this file,
        // and is still what the name may come to mean.
        if (Callee->hasLocalLinkage() && !Callee->isDeclaration())
          Kept.insert(Callee);
      }
      CB->setCalledFunction(Copy);
    }
  };
  for (Function *F : Undecided) {
    F->addFnAttr("pic16f1-dual");
    for (const char *Kind : {".sf", ".isr"}) {
      ValueToValueMapTy Map;
      Function *Static = CloneFunction(F, Map);
      Static->setName(F->getName() + Kind);
      Static->addFnAttr("pic16f1-dual-of", F->getName());
      Static->addFnAttr("pic16f1-static-frame");
      if (Kind == StringRef(".sf")) {
        F->addFnAttr("pic16f1-dual-sf", Static->getName());
      } else {
        Static->addFnAttr("pic16f1-isr-copy");
        CallInterruptCopies(*Static);
      }
      fixLocals(*Static);
      Versions.push_back(Static);
    }
    Changed = true;
  }
  if (DualFrames)
    for (Function &F : M)
      if (!F.isDeclaration() && F.hasFnAttribute("interrupt"))
        CallInterruptCopies(F);
  // Nothing in the module refers to the static versions; this keeps them
  // (code generation goes by the call graph).
  Versions.append(Kept.begin(), Kept.end());
  if (!Versions.empty())
    appendToCompilerUsed(M, Versions);
  return Changed;
}

/// The debug information says where a stack variable is by pointing at its
/// alloca. One that has moved to a fixed address is described the way a
/// static variable of the function is instead: a variable of the function's
/// own scope whose location is an address, here \p At bytes into \p GV.
static void describeForDebugger(Function &F, AllocaInst *AI,
                                GlobalVariable *GV, uint64_t At) {
  DISubprogram *SP = F.getSubprogram();
  if (!SP)
    return;
  LLVMContext &Ctx = F.getContext();
  for (DbgVariableRecord *DVR : findDVRDeclares(AI)) {
    DILocalVariable *Var = DVR->getVariable();
    DIExpression *Expr = DVR->getExpression();
    // Only a variable of this function itself that is the whole object or
    // a plain piece of it.
    bool Plain = Expr->getNumElements() == 0 || Expr->isFragment();
    if (!DVR->getDebugLoc().getInlinedAt() && Plain &&
        Var->getScope()->getSubprogram() == SP) {
      auto *DGV = DIGlobalVariable::getDistinct(
          Ctx, Var->getScope(), Var->getName(), "", Var->getFile(),
          Var->getLine(), Var->getType(), /*IsLocalToUnit=*/true,
          /*IsDefinition=*/true, nullptr, nullptr, Var->getAlignInBits(),
          nullptr);
      DIExpression *Where = Expr;
      if (At != 0)
        Where = DIExpression::prepend(Expr, 0, At);
      auto *GVE = DIGlobalVariableExpression::get(Ctx, DGV, Where);
      GV->addDebugInfo(GVE);
      // In place of the stack variable's own entry, which would be left
      // as a variable without a location.
      SmallVector<Metadata *, 8> Nodes;
      for (MDNode *N : SP->getRetainedNodes())
        if (N != Var)
          Nodes.push_back(N);
      Nodes.push_back(GVE);
      SP->replaceRetainedNodes(MDNode::get(Ctx, Nodes));
    }
    DVR->eraseFromParent();
  }
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
      describeForDebugger(F, AI, GV, At);
      Constant *Addr = ConstantExpr::getInBoundsGetElementPtr(
          I8, GV, ConstantInt::get(Type::getInt16Ty(M.getContext()), At));
      AI->replaceAllUsesWith(Addr);
      AI->eraseFromParent();
    }
  }
  return !Locals.empty();
}
