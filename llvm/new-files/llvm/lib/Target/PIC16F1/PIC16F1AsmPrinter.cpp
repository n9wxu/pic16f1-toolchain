//===-- PIC16F1AsmPrinter.cpp - Emit machine code --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PIC16F1InstPrinter.h"
#include "MCTargetDesc/PIC16F1MCAsmInfo.h"
#include "PIC16F1.h"
#include "PIC16F1TargetMachine.h"
#include "TargetInfo/PIC16F1TargetInfo.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Mangler.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

namespace {
class PIC16F1AsmPrinter : public AsmPrinter {
public:
  PIC16F1AsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID) {}

  StringRef getPassName() const override { return "PIC16F1 Assembly Printer"; }

  void emitInstruction(const MachineInstr *MI) override;
  void emitFunctionBodyEnd() override;
  void emitEndOfAsmFile(Module &M) override;
  const MCExpr *lowerConstant(const Constant *CV, const Constant *BaseCV,
                              uint64_t Offset) override;

  static char ID;

private:
  MCOperand lowerSymbol(const MachineOperand &MO, const MCSymbol *Sym) const;

  /// What the linker needs to tell which functions can be re-entered: see
  /// emitFunctionBodyEnd.
  std::string FrameRecords;
};
} // end of anonymous namespace

static uint16_t specifierForFlags(unsigned Flags) {
  switch (Flags) {
  case PIC16F1II::MO_LO8:
    return PIC16F1::S_LO8;
  case PIC16F1II::MO_HI8:
    return PIC16F1::S_HI8;
  case PIC16F1II::MO_PM_LO8:
    return PIC16F1::S_PM_LO8;
  case PIC16F1II::MO_PM_HI8:
    return PIC16F1::S_PM_HI8;
  case PIC16F1II::MO_BANK:
    return PIC16F1::S_BANK;
  case PIC16F1II::MO_PAGE:
    return PIC16F1::S_PAGE;
  default:
    return PIC16F1::S_None;
  }
}

MCOperand PIC16F1AsmPrinter::lowerSymbol(const MachineOperand &MO,
                                         const MCSymbol *Sym) const {
  const MCExpr *Expr = MCSymbolRefExpr::create(Sym, OutContext);
  if (!MO.isMBB() && MO.getOffset())
    Expr = MCBinaryExpr::createAdd(
        Expr, MCConstantExpr::create(MO.getOffset(), OutContext), OutContext);
  if (uint16_t Spec = specifierForFlags(MO.getTargetFlags()))
    Expr = MCSpecifierExpr::create(Expr, Spec, OutContext);
  return MCOperand::createExpr(Expr);
}

void PIC16F1AsmPrinter::emitInstruction(const MachineInstr *MI) {
  if (MI->getOpcode() == PIC16F1::BRTABLEW) {
    // BRW, then a jump for each value of the index.
    MCInst Skip;
    Skip.setOpcode(PIC16F1::BRW);
    EmitToStreamer(*OutStreamer, Skip);
    bool OnePage = TM.getCodeModel() == CodeModel::Small;
    const MachineJumpTableEntry &Table =
        MF->getJumpTableInfo()->getJumpTables()[MI->getOperand(0).getIndex()];
    for (const MachineBasicBlock *Target : Table.MBBs) {
      const MCExpr *To = MCSymbolRefExpr::create(Target->getSymbol(), OutContext);
      if (!OnePage) {
        MCInst Page;
        Page.setOpcode(PIC16F1::MOVLP);
        Page.addOperand(MCOperand::createExpr(
            MCSpecifierExpr::create(To, PIC16F1::S_PAGE, OutContext)));
        EmitToStreamer(*OutStreamer, Page);
      }
      MCInst Jump;
      Jump.setOpcode(PIC16F1::GOTO);
      Jump.addOperand(MCOperand::createExpr(To));
      EmitToStreamer(*OutStreamer, Jump);
    }
    return;
  }
  if (MI->isPseudo()) {
    MI->print(errs());
    report_fatal_error("pseudo instruction reached the assembly printer");
  }
  MCInst Out;
  Out.setOpcode(MI->getOpcode());
  for (const MachineOperand &MO : MI->operands()) {
    switch (MO.getType()) {
    case MachineOperand::MO_Register:
    case MachineOperand::MO_RegisterMask:
      // Machine instructions take file addresses, never registers. Register
      // operands here are implicit ones kept for liveness.
      continue;
    case MachineOperand::MO_Immediate:
      Out.addOperand(MCOperand::createImm(MO.getImm()));
      break;
    case MachineOperand::MO_MachineBasicBlock:
      Out.addOperand(lowerSymbol(MO, MO.getMBB()->getSymbol()));
      break;
    case MachineOperand::MO_GlobalAddress:
      Out.addOperand(lowerSymbol(MO, getSymbol(MO.getGlobal())));
      break;
    case MachineOperand::MO_ExternalSymbol:
      Out.addOperand(
          lowerSymbol(MO, GetExternalSymbolSymbol(MO.getSymbolName())));
      break;
    case MachineOperand::MO_BlockAddress:
      Out.addOperand(
          lowerSymbol(MO, GetBlockAddressSymbol(MO.getBlockAddress())));
      break;
    default:
      MI->print(errs());
      report_fatal_error("unsupported operand type");
    }
  }
  EmitToStreamer(*OutStreamer, Out);
}

/// A record of the function for the linker, which chooses between the stack
/// and static versions of a function (PIC16F1StaticFrames.cpp; lld,
/// PIC16F1Frames.cpp) and for that needs every call in the program. One
/// line each, in the section .pic16f1.frames:
///   F name flags   a function; flags: i interrupt handler, p calls through
///                  a pointer, - none
///   V name static  the static version of the function "name"
///   C name         a call from the function of the last F or V line
///   J name         a jump from it to a function that returns in its place:
///                  a call for every purpose but the hardware stack, of
///                  which it takes no level
///   A name         a function whose address is taken (anywhere)
///   I alias name   "alias" stands for the function "name" when called from
///                  code that runs in an interrupt handler
///   V name static K   with K = i: the static version for use in a handler
void PIC16F1AsmPrinter::emitFunctionBodyEnd() {
  const Function &F = MF->getFunction();
  raw_string_ostream OS(FrameRecords);
  StringRef Name = CurrentFnSym->getName();
  if (F.hasFnAttribute("pic16f1-dual-of")) {
    OS << "V " << F.getFnAttribute("pic16f1-dual-of").getValueAsString() << ' '
       << Name << (F.hasFnAttribute("pic16f1-isr-copy") ? " i" : " s") << '\n';
  } else {
    bool Indirect = false;
    for (const MachineBasicBlock &MBB : *MF)
      for (const MachineInstr &MI : MBB)
        if (MI.isCall())
          Indirect |= llvm::none_of(MI.operands(), [](const MachineOperand &MO) {
            return MO.isGlobal() || MO.isSymbol();
          });
    OS << "F " << Name << ' '
       << (F.hasFnAttribute("interrupt") ? "i" : Indirect ? "p" : "-") << '\n';
  }
  for (const MachineBasicBlock &MBB : *MF) {
    for (const MachineInstr &MI : MBB) {
      // A jump to a symbol is a call that will not come back.
      bool Calls = MI.isCall() || MI.getOpcode() == PIC16F1::GOTO;
      for (const MachineOperand &MO : MI.operands()) {
        // The page selection in front of a call names the callee too.
        if ((MO.isGlobal() || MO.isSymbol()) &&
            MO.getTargetFlags() == PIC16F1II::MO_PAGE)
          continue;
        if (MO.isGlobal() && isa<Function>(MO.getGlobal()))
          OS << (!Calls ? "A " : MI.isCall() ? "C " : "J ")
             << getSymbol(MO.getGlobal())->getName() << '\n';
        else if (MO.isSymbol() && Calls)
          OS << (MI.isCall() ? "C " : "J ")
             << GetExternalSymbolSymbol(MO.getSymbolName())->getName() << '\n';
      }
    }
  }
}

/// Functions whose address is in the initial value of a variable.
static void addressesIn(const Constant *C, SmallPtrSetImpl<const Constant *> &Seen,
                        SmallVectorImpl<const Function *> &Out) {
  if (!Seen.insert(C).second)
    return;
  if (const auto *F = dyn_cast<Function>(C)) {
    Out.push_back(F);
    return;
  }
  if (isa<GlobalValue>(C))
    return;
  for (const Use &Op : C->operands())
    if (const auto *OC = dyn_cast<Constant>(Op))
      addressesIn(OC, Seen, Out);
}

/// The startup code copies initialised data to RAM and clears .bss only if
/// something asks for it: those loops are library members that define
/// __do_copy_data and __do_clear_bss. Refer to the one this module needs.
void PIC16F1AsmPrinter::emitEndOfAsmFile(Module &M) {
  {
    SmallPtrSet<const Constant *, 16> Seen;
    SmallVector<const Function *, 8> Taken;
    for (const GlobalVariable &GV : M.globals())
      if (GV.hasInitializer())
        addressesIn(GV.getInitializer(), Seen, Taken);
    raw_string_ostream OS(FrameRecords);
    for (const Function *F : Taken)
      OS << "A " << getSymbol(F)->getName() << '\n';
    for (const Function &F : M)
      if (F.hasFnAttribute("pic16f1-isr-of"))
        if (const Function *Of = M.getFunction(
                F.getFnAttribute("pic16f1-isr-of").getValueAsString()))
          OS << "I " << getSymbol(&F)->getName() << ' '
             << getSymbol(Of)->getName() << '\n';
  }
  if (!FrameRecords.empty()) {
    OutStreamer->switchSection(
        OutContext.getELFSection(".pic16f1.frames", ELF::SHT_PROGBITS, 0));
    OutStreamer->emitBytes(FrameRecords);
    FrameRecords.clear();
  }
  bool NeedsCopy = false, NeedsClear = false;
  for (const GlobalVariable &GV : M.globals()) {
    // Not data of the program: llvm.used and the like.
    if (GV.isDeclaration() || GV.hasAvailableExternallyLinkage() ||
        GV.getName().starts_with("llvm.") || GV.getSection() == "llvm.metadata")
      continue;
    // Variables with fixed addresses in place of a stack frame have no
    // initial value.
    if (GV.getSection().starts_with(".noinit"))
      continue;
    SectionKind Kind = TargetLoweringObjectFile::getKindForGlobal(&GV, TM);
    if (Kind.isBSS() || Kind.isCommon())
      NeedsClear = true;
    else if (Kind.isWriteable())
      NeedsCopy = true;
    // A section the program names itself may be an input of either
    // output section.
    if (GV.hasSection() && Kind.isWriteable())
      NeedsCopy = NeedsClear = true;
  }
  // Code that never sets PCLATH is only right when the whole program lies
  // in the first page. This reference makes the linker script check that.
  if (TM.getCodeModel() == CodeModel::Small)
    OutStreamer->emitSymbolAttribute(
        OutContext.getOrCreateSymbol("__pic16f1_one_page"), MCSA_Global);
  if (NeedsCopy)
    OutStreamer->emitSymbolAttribute(
        OutContext.getOrCreateSymbol("__do_copy_data"), MCSA_Global);
  if (NeedsClear)
    OutStreamer->emitSymbolAttribute(
        OutContext.getOrCreateSymbol("__do_clear_bss"), MCSA_Global);
  // Likewise the piece that runs the constructors of global objects (C++,
  // and __attribute__((constructor))), listed in .init_array.
  if (const GlobalVariable *Ctors = M.getGlobalVariable("llvm.global_ctors"))
    if (Ctors->hasInitializer() &&
        isa<ConstantArray>(Ctors->getInitializer()) &&
        Ctors->getInitializer()->getNumOperands() != 0)
      OutStreamer->emitSymbolAttribute(
          OutContext.getOrCreateSymbol("__do_global_ctors"), MCSA_Global);
}

const MCExpr *PIC16F1AsmPrinter::lowerConstant(const Constant *CV,
                                               const Constant *BaseCV,
                                               uint64_t Offset) {
  // A function pointer stored in data is a word address.
  const MCSymbol *Sym = nullptr;
  if (const auto *F = dyn_cast<Function>(CV))
    Sym = getSymbol(F);
  else if (const auto *BA = dyn_cast<BlockAddress>(CV))
    Sym = GetBlockAddressSymbol(BA);
  if (Sym)
    return MCSpecifierExpr::create(MCSymbolRefExpr::create(Sym, OutContext),
                                   PIC16F1::S_PM, OutContext);
  return AsmPrinter::lowerConstant(CV, BaseCV, Offset);
}

char PIC16F1AsmPrinter::ID = 0;

INITIALIZE_PASS(PIC16F1AsmPrinter, "pic16f1-asm-printer",
                "PIC16F1 Assembly Printer", false, false)

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializePIC16F1AsmPrinter() {
  RegisterAsmPrinter<PIC16F1AsmPrinter> X(getThePIC16F1Target());
}
