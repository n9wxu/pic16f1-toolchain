//===-- PIC16F1LegalizerInfo.cpp - Legalizer rules ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The legal types are s8, s16 and the 16-bit pointer p0: a byte register, a
// register pair, and a pair holding an address. Wider integers are narrowed
// to 16-bit pieces; add and subtract then chain through the carry flag.
//
//===----------------------------------------------------------------------===//

#include "PIC16F1LegalizerInfo.h"
#include "PIC16F1MachineFunctionInfo.h"
#include "PIC16F1Subtarget.h"
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/GlobalISel/LegalizerHelper.h"
#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Casting.h"

using namespace llvm;
using namespace TargetOpcode;
using namespace LegalityPredicates;

PIC16F1LegalizerInfo::PIC16F1LegalizerInfo(const PIC16F1Subtarget &ST) {
  const LLT s1 = LLT::scalar(1);
  const LLT s8 = LLT::scalar(8);
  const LLT s16 = LLT::scalar(16);
  const LLT s32 = LLT::scalar(32);
  const LLT s64 = LLT::scalar(64);
  const LLT p0 = LLT::pointer(0, 16);

  // Values.
  getActionDefinitionsBuilder({G_CONSTANT, G_IMPLICIT_DEF, G_FREEZE, G_PHI})
      .legalFor({s8, s16, p0})
      .widenScalarToNextPow2(0, 8)
      .clampScalar(0, s8, s16);

  getActionDefinitionsBuilder({G_GLOBAL_VALUE, G_FRAME_INDEX, G_BLOCK_ADDR})
      .legalFor({p0});

  // Arithmetic and logic. Wider operations are split into 16-bit ones; for
  // add and subtract that produces a G_UADDO/G_UADDE chain.
  getActionDefinitionsBuilder({G_ADD, G_SUB, G_AND, G_OR, G_XOR})
      .legalFor({s8, s16})
      .widenScalarToNextPow2(0, 8)
      .clampScalar(0, s8, s16);

  // The carry between the links of a chain is s1. It never becomes a
  // register: the selector keeps it in the carry flag.
  getActionDefinitionsBuilder({G_UADDO, G_UADDE, G_USUBO, G_USUBE})
      .legalFor({{s16, s1}, {s16, s8}})
      .widenScalarToNextPow2(0, 16)
      .clampScalar(0, s16, s16);

  // No multiply or divide instruction exists. The generic library-call
  // path knows only 32 bits and wider, so 8 and 16 bits go through
  // legalizeCustom.
  getActionDefinitionsBuilder({G_MUL, G_SDIV, G_UDIV, G_SREM, G_UREM})
      .customFor({s8, s16, s32, s64})
      .widenScalarToNextPow2(0, 8)
      .clampScalar(0, s8, s64);

  // Shifts. The amount is a byte. Narrow shifts are selected directly, as a
  // run of one-bit shifts or a loop; wide ones are handled in legalizeCustom.
  getActionDefinitionsBuilder({G_SHL, G_LSHR, G_ASHR})
      .legalFor({{s8, s8}, {s16, s8}})
      .customFor({{s32, s8}, {s64, s8}})
      .clampScalar(1, s8, s8)
      .widenScalarToNextPow2(0, 8)
      .clampScalar(0, s8, s64);

  // Extension and truncation.
  getActionDefinitionsBuilder({G_ZEXT, G_SEXT, G_ANYEXT})
      .legalFor({{s16, s8}})
      .customIf([=](const LegalityQuery &Q) {
        return Q.Types[0].getSizeInBits() > 16 &&
               (Q.Types[1] == s8 || Q.Types[1] == s16 || Q.Types[1] == s32);
      })
      .widenScalarToNextPow2(0, 8)
      .clampScalar(0, s8, s64);
  getActionDefinitionsBuilder(G_TRUNC)
      .legalFor({{s8, s16}})
      .customIf([=](const LegalityQuery &Q) {
        return Q.Types[1].getSizeInBits() > 16 &&
               Q.Types[1].getSizeInBits() % 16 == 0 &&
               Q.Types[0].getSizeInBits() % 8 == 0;
      });
  getActionDefinitionsBuilder(G_SEXT_INREG).lower();
  getActionDefinitionsBuilder({G_UBFX, G_SBFX}).custom();

  getActionDefinitionsBuilder(G_MERGE_VALUES).legalFor({{s16, s8}});
  getActionDefinitionsBuilder(G_UNMERGE_VALUES).legalFor({{s8, s16}});

  // Pointers.
  getActionDefinitionsBuilder(G_PTR_ADD)
      .legalFor({{p0, s16}})
      .clampScalar(1, s16, s16);
  getActionDefinitionsBuilder(G_PTRTOINT)
      .legalFor({{s16, p0}})
      .widenScalarToNextPow2(0, 16)
      .clampScalar(0, s16, s16);
  getActionDefinitionsBuilder(G_INTTOPTR)
      .legalFor({{p0, s16}})
      .clampScalar(1, s16, s16);

  // Memory. A load or store moves one or two bytes; anything else is split,
  // and one whose register is wider than the memory access is lowered to a
  // plain access and an extension or truncation.
  auto MemSizeDiffers = [](const LegalityQuery &Q) {
    return Q.Types[0].getSizeInBits() != Q.MMODescrs[0].MemoryTy.getSizeInBits();
  };
  getActionDefinitionsBuilder({G_LOAD, G_STORE})
      .legalForTypesWithMemDesc({{s8, p0, s8, 8}, {s16, p0, s16, 8}, {p0, p0, p0, 8}})
      .lowerIf(MemSizeDiffers)
      .widenScalarToNextPow2(0, 8)
      .clampScalar(0, s8, s16);
  getActionDefinitionsBuilder({G_SEXTLOAD, G_ZEXTLOAD}).custom();
  getActionDefinitionsBuilder({G_MEMCPY, G_MEMMOVE, G_MEMSET}).libcall();

  // Comparison, selection and control flow. A comparison yields a byte
  // holding 0 or 1.
  getActionDefinitionsBuilder(G_ICMP)
      .legalFor({{s8, s8}, {s8, s16}, {s8, p0}})
      .clampScalar(0, s8, s8)
      .customFor({{s8, s32}, {s8, s64}})
      .widenScalarToNextPow2(1, 8)
      .clampScalar(1, s8, s16);
  getActionDefinitionsBuilder(G_SELECT)
      .legalFor({{s8, s8}, {s16, s8}, {p0, s8}})
      .clampScalar(1, s8, s8)
      .widenScalarToNextPow2(0, 8)
      .clampScalar(0, s8, s16);
  getActionDefinitionsBuilder(G_BRCOND).legalFor({s8}).clampScalar(0, s8, s8);
  getActionDefinitionsBuilder(G_BR).alwaysLegal();
  getActionDefinitionsBuilder(G_BRINDIRECT).legalFor({p0});
  getActionDefinitionsBuilder(G_BRJT).legalFor({{p0, s16}});
  getActionDefinitionsBuilder(G_JUMP_TABLE).legalFor({p0});

  // Operations with no instruction of their own become simpler ones.
  getActionDefinitionsBuilder(
      {G_BSWAP, G_BITREVERSE, G_CTPOP, G_CTLZ, G_CTLZ_ZERO_POISON, G_CTTZ,
       G_CTTZ_ZERO_POISON, G_ROTL, G_ROTR, G_FSHL, G_FSHR, G_ABS, G_SMIN,
       G_SMAX, G_UMIN, G_UMAX, G_UMULO, G_SMULO, G_SADDO,
       G_SSUBO, G_SADDE, G_SSUBE, G_UADDSAT, G_USUBSAT, G_SADDSAT, G_SSUBSAT,
       G_SDIVREM})
      .lower();
  // The unsigned divide routines leave the remainder behind as well, so a
  // quotient and remainder together are one call (legalizeCustom).
  getActionDefinitionsBuilder(G_UDIVREM).customFor({s8, s16, s32}).lower();

  // The high half of a product comes from a multiply of twice the width,
  // which exists as a library routine up to 64 bits.
  getActionDefinitionsBuilder({G_UMULH, G_SMULH})
      .lowerFor({s8, s16, s32})
      .widenScalarToNextPow2(0, 8)
      .clampScalar(0, s8, s32);

  // Floating point is single precision in software. A float is carried as
  // a 32-bit integer and every operation is a runtime call.
  getActionDefinitionsBuilder({G_FADD, G_FSUB, G_FMUL, G_FDIV})
      .libcallFor({s32});
  getActionDefinitionsBuilder(G_FCMP)
      .libcallFor({{s8, s32}})
      .clampScalar(0, s8, s8);
  getActionDefinitionsBuilder({G_FPTOSI, G_FPTOUI})
      .libcallFor({{s32, s32}, {s64, s32}})
      .widenScalarToNextPow2(0, 32)
      .clampScalar(0, s32, s64);
  getActionDefinitionsBuilder({G_SITOFP, G_UITOFP})
      .libcallFor({{s32, s32}, {s32, s64}})
      .widenScalarToNextPow2(1, 32)
      .clampScalar(1, s32, s64);
  getActionDefinitionsBuilder(G_FCONSTANT).customFor({s32});
  getActionDefinitionsBuilder({G_FNEG, G_FABS, G_FCOPYSIGN}).lower();

  // Variadic functions.
  getActionDefinitionsBuilder(G_VASTART).customFor({p0});
  getActionDefinitionsBuilder(G_VAARG).lower();

  verify(*ST.getInstrInfo());
}

bool PIC16F1LegalizerInfo::legalizeCustom(LegalizerHelper &Helper,
                                          MachineInstr &MI,
                                          LostDebugLocObserver &LocObserver) const {
  switch (MI.getOpcode()) {
  case G_SHL:
  case G_LSHR:
  case G_ASHR:
    return legalizeWideShift(Helper, MI, LocObserver);
  case G_ZEXT:
  case G_SEXT:
  case G_ANYEXT:
    return legalizeWideExt(Helper, MI);
  case G_VASTART:
    return legalizeVAStart(Helper, MI);
  case G_TRUNC:
    return legalizeWideTrunc(Helper, MI);
  case G_UBFX:
  case G_SBFX: {
    // A field of constant position and width: shift it down, then mask or
    // sign extend.
    MachineIRBuilder &B = Helper.MIRBuilder;
    MachineRegisterInfo &MRI = *B.getMRI();
    Register Dst = MI.getOperand(0).getReg();
    LLT Ty = MRI.getType(Dst);
    auto Lsb = getIConstantVRegValWithLookThrough(MI.getOperand(2).getReg(), MRI);
    auto Width = getIConstantVRegValWithLookThrough(MI.getOperand(3).getReg(), MRI);
    if (!Lsb || !Width)
      return false;
    unsigned W = Width->Value.getZExtValue();
    auto Amt = B.buildConstant(Ty, Lsb->Value.getZExtValue());
    if (MI.getOpcode() == G_UBFX) {
      auto Sh = B.buildLShr(Ty, MI.getOperand(1).getReg(), Amt);
      B.buildAnd(Dst, Sh,
                 B.buildConstant(
                     Ty, APInt::getLowBitsSet(Ty.getSizeInBits(), W)));
    } else {
      auto Sh = B.buildAShr(Ty, MI.getOperand(1).getReg(), Amt);
      B.buildSExtInReg(Dst, Sh, W);
    }
    MI.eraseFromParent();
    return true;
  }
  case G_FCONSTANT: {
    // The same bits, as an integer.
    MachineIRBuilder &B = Helper.MIRBuilder;
    APInt Bits = MI.getOperand(1).getFPImm()->getValueAPF().bitcastToAPInt();
    B.buildConstant(MI.getOperand(0).getReg(), Bits);
    MI.eraseFromParent();
    return true;
  }
  case G_SEXTLOAD:
  case G_ZEXTLOAD: {
    // A plain load of the bytes in memory, then the extension.
    MachineIRBuilder &B = Helper.MIRBuilder;
    auto &Load = cast<GExtLoad>(MI);
    LLT MemTy = Load.getMMO().getMemoryType();
    auto Narrow = B.buildLoad(MemTy, Load.getPointerReg(), Load.getMMO());
    B.buildInstr(MI.getOpcode() == G_SEXTLOAD ? G_SEXT : G_ZEXT,
                 {Load.getDstReg()}, {Narrow});
    MI.eraseFromParent();
    return true;
  }
  case G_ICMP:
    return legalizeWideICmp(Helper, MI);
  case G_MUL:
  case G_SDIV:
  case G_UDIV:
  case G_SREM:
  case G_UREM:
    return legalizeNarrowMulDiv(Helper, MI, LocObserver);
  case G_UDIVREM: {
    // __udivqi3 and __udivhi3 return the quotient in the first register
    // (pair) and leave the remainder in the third: to the compiler, a
    // function with three results of which the second is not used.
    // __udivmodsi4 returns the quotient and then the remainder.
    MachineIRBuilder &B = Helper.MIRBuilder;
    MachineRegisterInfo &MRI = *B.getMRI();
    Register Quot = MI.getOperand(0).getReg();
    Register Rem = MI.getOperand(1).getReg();
    LLT Ty = MRI.getType(Quot);
    unsigned Bits = Ty.getSizeInBits();
    LLVMContext &Ctx = B.getMF().getFunction().getContext();
    Type *IntTy = IntegerType::get(Ctx, Bits);
    SmallVector<Register, 3> Results;
    SmallVector<Type *, 3> Fields;
    Results.push_back(Quot);
    Fields.push_back(IntTy);
    if (Bits != 32) {
      Results.push_back(MRI.createGenericVirtualRegister(Ty));
      Fields.push_back(IntTy);
    }
    Results.push_back(Rem);
    Fields.push_back(IntTy);
    const char *Name = Bits == 8    ? "__udivqi3"
                       : Bits == 16 ? "__udivhi3"
                                    : "__udivmodsi4";
    auto Status = Helper.createLibcall(
        Name, {Results, StructType::get(Ctx, Fields), 0},
        {{MI.getOperand(2).getReg(), IntTy, 0},
         {MI.getOperand(3).getReg(), IntTy, 1}},
        CallingConv::C, LocObserver, &MI);
    if (Status != LegalizerHelper::Legalized)
      return false;
    MI.eraseFromParent();
    return true;
  }
  default:
    return false;
  }
}

/// Multiply, divide and remainder call the runtime library, except by a
/// power of two.
bool PIC16F1LegalizerInfo::legalizeNarrowMulDiv(
    LegalizerHelper &Helper, MachineInstr &MI,
    LostDebugLocObserver &LocObserver) const {
  MachineIRBuilder &B = Helper.MIRBuilder;
  MachineRegisterInfo &MRI = *B.getMRI();
  Register Dst = MI.getOperand(0).getReg();
  LLT Ty = MRI.getType(Dst);

  // By a power of two these are a shift or a mask. The combiner sees to
  // that when optimizing; this covers -O0 and whatever reaches here later.
  if (auto C = getIConstantVRegValWithLookThrough(MI.getOperand(2).getReg(),
                                                  MRI);
      C && C->Value.isPowerOf2()) {
    unsigned Log = C->Value.logBase2();
    Register X = MI.getOperand(1).getReg();
    const LLT s8 = LLT::scalar(8);
    switch (MI.getOpcode()) {
    case G_MUL:
      B.buildShl(Dst, X, B.buildConstant(s8, Log));
      MI.eraseFromParent();
      return true;
    case G_UDIV:
      B.buildLShr(Dst, X, B.buildConstant(s8, Log));
      MI.eraseFromParent();
      return true;
    case G_SDIV: {
      // Rounding is towards zero, so a negative value first gets the
      // divisor less one added. (A negative divisor is not a power of two
      // here, though its bit pattern may be.)
      if (Ty.getSizeInBits() > 16 || Log == 0 || !C->Value.isStrictlyPositive())
        break;
      auto Biased = B.buildInstr(PIC16F1::G_ADD_IF_NEG, {Ty}, {X})
                        .addImm((int64_t(1) << Log) - 1);
      B.buildAShr(Dst, Biased, B.buildConstant(s8, Log));
      MI.eraseFromParent();
      return true;
    }
    case G_UREM:
      B.buildAnd(Dst, X, B.buildConstant(Ty, C->Value - 1));
      MI.eraseFromParent();
      return true;
    default:
      break;
    }
  }
  if (Ty.getSizeInBits() > 16)
    return Helper.libcall(MI, LocObserver) == LegalizerHelper::Legalized;

  bool Is8 = Ty.getSizeInBits() == 8;
  RTLIB::Libcall LC;
  switch (MI.getOpcode()) {
  case G_MUL:
    LC = Is8 ? RTLIB::MUL_I8 : RTLIB::MUL_I16;
    break;
  case G_SDIV:
    LC = Is8 ? RTLIB::SDIV_I8 : RTLIB::SDIV_I16;
    break;
  case G_UDIV:
    LC = Is8 ? RTLIB::UDIV_I8 : RTLIB::UDIV_I16;
    break;
  case G_SREM:
    LC = Is8 ? RTLIB::SREM_I8 : RTLIB::SREM_I16;
    break;
  default:
    LC = Is8 ? RTLIB::UREM_I8 : RTLIB::UREM_I16;
    break;
  }
  Type *IRTy =
      IntegerType::get(B.getMF().getFunction().getContext(), Is8 ? 8 : 16);
  auto Status = Helper.createLibcall(
      LC, {Dst, IRTy, 0},
      {{MI.getOperand(1).getReg(), IRTy, 0},
       {MI.getOperand(2).getReg(), IRTy, 1}},
      LocObserver, &MI);
  if (Status != LegalizerHelper::Legalized)
    return false;
  MI.eraseFromParent();
  return true;
}

/// A 32- or 64-bit shift. Whole bytes and one or two bits are cheap when
/// split into 16-bit pieces; anything else costs several instructions per
/// bit for every byte, so call the runtime routine, which loops.
bool PIC16F1LegalizerInfo::legalizeWideShift(
    LegalizerHelper &Helper, MachineInstr &MI,
    LostDebugLocObserver &LocObserver) const {
  MachineIRBuilder &B = Helper.MIRBuilder;
  MachineRegisterInfo &MRI = *B.getMRI();
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  Register Amt = MI.getOperand(2).getReg();
  LLT Ty = MRI.getType(Dst);
  unsigned Bits = Ty.getSizeInBits();

  if (auto C = getIConstantVRegValWithLookThrough(Amt, MRI)) {
    uint64_t N = C->Value.getZExtValue();
    // A shift that leaves only one 16-bit piece of the result non-trivial,
    // such as "x >> 31" to test the top bit, is a 16-bit shift of one piece.
    if (N < Bits && N >= Bits - 16) {
      const LLT s8 = LLT::scalar(8);
      const LLT s16 = LLT::scalar(16);
      unsigned Pieces = Bits / 16;
      auto In = B.buildUnmerge(s16, Src);
      auto Rest = B.buildConstant(s8, N - (Bits - 16));
      SmallVector<Register, 4> Out(Pieces);
      if (MI.getOpcode() == G_SHL) {
        Register Zero = B.buildConstant(s16, 0).getReg(0);
        for (unsigned I = 0; I + 1 < Pieces; ++I)
          Out[I] = Zero;
        Out[Pieces - 1] = B.buildShl(s16, In.getReg(0), Rest).getReg(0);
      } else {
        bool Arith = MI.getOpcode() == G_ASHR;
        Register Top = In.getReg(Pieces - 1);
        Out[0] = B.buildInstr(MI.getOpcode(), {s16}, {Top, Rest}).getReg(0);
        Register Fill = Arith ? B.buildAShr(s16, Top, B.buildConstant(s8, 15)).getReg(0)
                              : B.buildConstant(s16, 0).getReg(0);
        for (unsigned I = 1; I < Pieces; ++I)
          Out[I] = Fill;
      }
      B.buildMergeLikeInstr(Dst, Out);
      MI.eraseFromParent();
      return true;
    }
    // A shift by one or two bits moves through the carry flag from one
    // 16-bit piece to the next. Left is x + x; right uses the rotates.
    if (N == 1 || N == 2) {
      const LLT s8 = LLT::scalar(8);
      const LLT s16 = LLT::scalar(16);
      unsigned Pieces = Bits / 16;
      auto In = B.buildUnmerge(s16, Src);
      SmallVector<Register, 4> P;
      for (unsigned I = 0; I != Pieces; ++I)
        P.push_back(In.getReg(I));
      for (uint64_t Step = 0; Step != N; ++Step) {
        Register Carry;
        if (MI.getOpcode() == G_SHL) {
          for (unsigned I = 0; I != Pieces; ++I) {
            auto Link =
                I == 0 ? B.buildInstr(G_UADDO, {s16, s8}, {P[I], P[I]})
                       : B.buildInstr(G_UADDE, {s16, s8}, {P[I], P[I], Carry});
            P[I] = Link.getReg(0);
            Carry = Link.getReg(1);
          }
        } else {
          unsigned Top =
              MI.getOpcode() == G_LSHR ? PIC16F1::G_LSR1 : PIC16F1::G_ASR1;
          for (unsigned I = Pieces; I-- != 0;) {
            auto Link =
                I == Pieces - 1
                    ? B.buildInstr(Top, {s16, s8}, {P[I]})
                    : B.buildInstr(PIC16F1::G_RRC, {s16, s8}, {P[I], Carry});
            P[I] = Link.getReg(0);
            Carry = Link.getReg(1);
          }
        }
      }
      B.buildMergeLikeInstr(Dst, P);
      MI.eraseFromParent();
      return true;
    }
    // Splitting works on halves; a 64-bit shift becomes 32-bit ones, which
    // come back here.
    if (N < Bits && N % 8 == 0)
      return Helper.narrowScalar(MI, 0, LLT::scalar(Bits / 2)) ==
             LegalizerHelper::Legalized;
  }

  RTLIB::Libcall LC;
  switch (MI.getOpcode()) {
  case G_SHL:
    LC = Bits == 32 ? RTLIB::SHL_I32 : RTLIB::SHL_I64;
    break;
  case G_LSHR:
    LC = Bits == 32 ? RTLIB::SRL_I32 : RTLIB::SRL_I64;
    break;
  default:
    LC = Bits == 32 ? RTLIB::SRA_I32 : RTLIB::SRA_I64;
    break;
  }
  LLVMContext &Ctx = B.getMF().getFunction().getContext();
  Type *ValTy = IntegerType::get(Ctx, Bits);
  // The routines take the count as an int.
  Register Amt16 = B.buildZExt(LLT::scalar(16), Amt).getReg(0);
  auto Status = Helper.createLibcall(
      LC, {Dst, ValTy, 0},
      {{Src, ValTy, 0}, {Amt16, Type::getInt16Ty(Ctx), 1}}, LocObserver, &MI);
  if (Status != LegalizerHelper::Legalized)
    return false;
  MI.eraseFromParent();
  return true;
}

/// Extend a byte or a 16-bit value to 32 or 64 bits: the value in the low
/// piece, then zeros, copies of the sign, or undefined pieces.
bool PIC16F1LegalizerInfo::legalizeWideExt(LegalizerHelper &Helper,
                                           MachineInstr &MI) const {
  MachineIRBuilder &B = Helper.MIRBuilder;
  MachineRegisterInfo &MRI = *B.getMRI();
  const LLT s8 = LLT::scalar(8);
  const LLT s16 = LLT::scalar(16);
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  unsigned Opc = MI.getOpcode();
  unsigned Pieces = MRI.getType(Dst).getSizeInBits() / 16;

  // The pieces of the source, lowest first.
  SmallVector<Register, 4> Parts;
  if (MRI.getType(Src) == s8) {
    Parts.push_back(B.buildInstr(Opc, {s16}, {Src}).getReg(0));
  } else if (MRI.getType(Src) == s16) {
    Parts.push_back(Src);
  } else {
    auto In = B.buildUnmerge(s16, Src);
    for (unsigned I = 0, E = In->getNumOperands() - 1; I != E; ++I)
      Parts.push_back(In.getReg(I));
  }

  Register Fill;
  if (Opc == G_ZEXT)
    Fill = B.buildConstant(s16, 0).getReg(0);
  else if (Opc == G_SEXT)
    Fill = B.buildAShr(s16, Parts.back(), B.buildConstant(s8, 15)).getReg(0);
  else
    Fill = B.buildUndef(s16).getReg(0);

  while (Parts.size() != Pieces)
    Parts.push_back(Fill);
  B.buildMergeLikeInstr(Dst, Parts);
  MI.eraseFromParent();
  return true;
}

/// Compare 32- or 64-bit values. Equality is a comparison of each piece.
/// An ordering is the borrow out of a subtraction chained through the carry
/// flag, which costs one subtract per 16 bits and no branches.
bool PIC16F1LegalizerInfo::legalizeWideICmp(LegalizerHelper &Helper,
                                            MachineInstr &MI) const {
  MachineIRBuilder &B = Helper.MIRBuilder;
  const LLT s8 = LLT::scalar(8);
  const LLT s16 = LLT::scalar(16);
  auto Pred = static_cast<CmpInst::Predicate>(MI.getOperand(1).getPredicate());
  Register Dst = MI.getOperand(0).getReg();
  Register L = MI.getOperand(2).getReg();
  Register R = MI.getOperand(3).getReg();

  // The sign of a wide value is the sign of its top piece.
  if (auto K = getIConstantVRegValWithLookThrough(R, *B.getMRI())) {
    bool Neg = Pred == CmpInst::ICMP_SLT && K->Value.isZero();
    bool NonNeg = (Pred == CmpInst::ICMP_SGE && K->Value.isZero()) ||
                  (Pred == CmpInst::ICMP_SGT && K->Value.isAllOnes());
    if (Neg || NonNeg) {
      auto Pieces = B.buildUnmerge(s16, L);
      Register Top = Pieces.getReg(Pieces->getNumOperands() - 2);
      B.buildICmp(Neg ? CmpInst::ICMP_SLT : CmpInst::ICMP_SGE, Dst, Top,
                  B.buildConstant(s16, 0));
      MI.eraseFromParent();
      return true;
    }
  }

  auto LP = B.buildUnmerge(s16, L);
  auto RP = B.buildUnmerge(s16, R);
  unsigned N = LP->getNumOperands() - 1;

  if (CmpInst::isEquality(Pred)) {
    // The values are equal when every piece is: OR the differences.
    Register Any;
    for (unsigned I = 0; I != N; ++I) {
      Register Diff = B.buildXor(s16, LP.getReg(I), RP.getReg(I)).getReg(0);
      Any = I ? B.buildOr(s16, Any, Diff).getReg(0) : Diff;
    }
    B.buildICmp(Pred, Dst, Any, B.buildConstant(s16, 0));
    MI.eraseFromParent();
    return true;
  }

  SmallVector<Register, 4> X, Y;
  for (unsigned I = 0; I != N; ++I) {
    X.push_back(LP.getReg(I));
    Y.push_back(RP.getReg(I));
  }
  // Flipping the sign bits turns a signed order into an unsigned one.
  if (CmpInst::isSigned(Pred)) {
    auto Sign = B.buildConstant(s16, 0x8000);
    X[N - 1] = B.buildXor(s16, X[N - 1], Sign).getReg(0);
    Y[N - 1] = B.buildXor(s16, Y[N - 1], Sign).getReg(0);
    Pred = ICmpInst::getUnsignedPredicate(Pred);
  }
  // Reduce to "x < y", possibly inverted: x - y borrows exactly then.
  bool Invert = Pred == CmpInst::ICMP_UGE || Pred == CmpInst::ICMP_ULE;
  if (Pred == CmpInst::ICMP_UGT || Pred == CmpInst::ICMP_ULE)
    std::swap(X, Y);

  Register Borrow;
  for (unsigned I = 0; I != N; ++I) {
    auto Step = I == 0 ? B.buildInstr(TargetOpcode::G_USUBO, {s16, s8},
                                      {X[I], Y[I]})
                       : B.buildInstr(TargetOpcode::G_USUBE, {s16, s8},
                                      {X[I], Y[I], Borrow});
    Borrow = Step.getReg(1);
  }
  if (Invert)
    B.buildXor(Dst, Borrow, B.buildConstant(s8, 1));
  else
    B.buildCopy(Dst, Borrow);
  MI.eraseFromParent();
  return true;
}

/// Truncate a 32- or 64-bit value: split it into 16-bit pieces and keep the
/// low ones.
bool PIC16F1LegalizerInfo::legalizeWideTrunc(LegalizerHelper &Helper,
                                             MachineInstr &MI) const {
  MachineIRBuilder &B = Helper.MIRBuilder;
  MachineRegisterInfo &MRI = *B.getMRI();
  const LLT s16 = LLT::scalar(16);
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  unsigned DstBits = MRI.getType(Dst).getSizeInBits();
  auto Pieces = B.buildUnmerge(s16, Src);
  if (DstBits == 8) {
    B.buildTrunc(Dst, Pieces.getReg(0));
  } else if (DstBits == 16) {
    B.buildCopy(Dst, Pieces.getReg(0));
  } else {
    SmallVector<Register, 4> Low;
    for (unsigned I = 0; I != DstBits / 16; ++I)
      Low.push_back(Pieces.getReg(I));
    B.buildMergeLikeInstr(Dst, Low);
  }
  MI.eraseFromParent();
  return true;
}

/// va_start stores the address of the first variable argument, which is on
/// the stack just past the named ones.
bool PIC16F1LegalizerInfo::legalizeVAStart(LegalizerHelper &Helper,
                                           MachineInstr &MI) const {
  MachineIRBuilder &B = Helper.MIRBuilder;
  MachineFunction &MF = B.getMF();
  int FI = MF.getInfo<PIC16F1MachineFunctionInfo>()->getVarArgsFrameIndex();
  const LLT p0 = LLT::pointer(0, 16);
  auto Addr = B.buildFrameIndex(p0, FI);
  B.buildStore(Addr, MI.getOperand(0).getReg(),
               *MF.getMachineMemOperand(MachinePointerInfo(),
                                        MachineMemOperand::MOStore, p0,
                                        Align(1)));
  MI.eraseFromParent();
  return true;
}
