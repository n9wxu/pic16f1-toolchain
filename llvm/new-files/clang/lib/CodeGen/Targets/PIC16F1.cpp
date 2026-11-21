//===- PIC16F1.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"
#include "llvm/Support/Casting.h"

using namespace clang;
using namespace clang::CodeGen;

namespace {

/// Scalars are passed as they are, with no promotion to int: the backend
/// assigns each byte to a register. A struct or union argument is passed as
/// a pointer to a copy the caller makes; one returned is written through a
/// pointer the caller supplies.
class PIC16F1ABIInfo : public DefaultABIInfo {
public:
  PIC16F1ABIInfo(CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}

  ABIArgInfo classifyReturnType(QualType RetTy) const {
    if (RetTy->isVoidType())
      return ABIArgInfo::getIgnore();
    if (isAggregateTypeForABI(RetTy) || RetTy->isAnyComplexType())
      return getNaturalAlignIndirect(RetTy,
                                     getDataLayout().getAllocaAddrSpace());
    if (const auto *ED = RetTy->getAsEnumDecl())
      RetTy = ED->getIntegerType();
    return ABIArgInfo::getDirect();
  }

  ABIArgInfo classifyArgumentType(QualType Ty) const {
    Ty = useFirstFieldIfTransparentUnion(Ty);
    if (isAggregateTypeForABI(Ty) || Ty->isAnyComplexType())
      return getNaturalAlignIndirect(Ty, getDataLayout().getAllocaAddrSpace(),
                                     /*ByVal=*/false);
    return ABIArgInfo::getDirect();
  }

  void computeInfo(CGFunctionInfo &FI) const override {
    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
    for (auto &I : FI.arguments())
      I.info = classifyArgumentType(I.type);
  }

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override {
    return CGF.EmitLoadOfAnyValue(
        CGF.MakeAddrLValue(
            EmitVAArgInstr(CGF, VAListAddr, Ty, classifyArgumentType(Ty)), Ty),
        Slot);
  }
};

class PIC16F1TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  PIC16F1TargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<PIC16F1ABIInfo>(CGT)) {}

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &M) const override {
    if (GV->isDeclaration())
      return;
    const auto *FD = dyn_cast_or_null<FunctionDecl>(D);
    if (!FD || !FD->hasAttr<PIC16F1InterruptAttr>())
      return;
    auto *F = cast<llvm::Function>(GV);
    F->addFnAttr("interrupt");
    F->addFnAttr(llvm::Attribute::NoInline);
  }
};

} // namespace

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createPIC16F1TargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<PIC16F1TargetCodeGenInfo>(CGM.getTypes());
}
