//===-- ABIPIC16F1.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIPIC16F1.h"

#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Status.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/TargetParser/Triple.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(ABIPIC16F1, ABIPIC16F1)

// DWARF numbers of the registers the compiler does not describe: it has no
// use for them in a variable's location, but unwinding does.
enum {
  dwarf_fsr1 = 23,
  dwarf_status = 26,
  dwarf_pc = 32,
  dwarf_stkptr = 33,
  dwarf_bsr = 34,
  dwarf_pclath = 35,
};

ABISP ABIPIC16F1::CreateInstance(ProcessSP process_sp, const ArchSpec &arch) {
  if (arch.GetTriple().getArch() != llvm::Triple::pic16f1)
    return ABISP();
  return ABISP(new ABIPIC16F1(std::move(process_sp), MakeMCRegisterInfo(arch)));
}

void ABIPIC16F1::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "ABI for the PIC16F1 enhanced mid-range core",
                                CreateInstance);
}

void ABIPIC16F1::Terminate() { PluginManager::UnregisterPlugin(CreateInstance); }

uint32_t ABIPIC16F1::GetGenericNum(llvm::StringRef reg) {
  if (reg == "pc")
    return LLDB_REGNUM_GENERIC_PC;
  // FSR1 is the stack pointer of a function that has a stack frame.
  if (reg == "fsr1")
    return LLDB_REGNUM_GENERIC_SP;
  if (reg == "status")
    return LLDB_REGNUM_GENERIC_FLAGS;
  return LLDB_INVALID_REGNUM;
}

void ABIPIC16F1::AugmentRegisterInfo(
    std::vector<DynamicRegisterInfo::Register> &regs) {
  // r0-r13, tmp0, tmp1, fsr0, fsr1 and w have the compiler's numbers.
  MCBasedABI::AugmentRegisterInfo(regs);

  auto find = [&](llvm::StringRef name) -> DynamicRegisterInfo::Register * {
    for (auto &r : regs)
      if (r.name.GetStringRef() == name)
        return &r;
    return nullptr;
  };
  const std::pair<const char *, uint32_t> Extra[] = {
      {"pc", dwarf_pc},         {"stkptr", dwarf_stkptr}, {"bsr", dwarf_bsr},
      {"pclath", dwarf_pclath}, {"status", dwarf_status},
      // The compiler's names for these differ from the debug server's.
      {"fsr1", dwarf_fsr1},     {"fsr0", 24},             {"w", 25},
  };
  for (const auto &[name, num] : Extra)
    if (auto *r = find(name))
      r->regnum_dwarf = num;

  if (find("rp0"))
    return;

  // Show the core's registers under the names the data sheet gives them.
  // (Names are matched without regard to case, and W keeps its short one.)
  // What the compiler calls r0-r13, tmp0 and tmp1 is, to the data sheet,
  // the common RAM at 0x70-0x7F: they stay registers, because the debug
  // information places variables in them, but in a group that says so.
  const ConstString core("Core registers");
  const ConstString common("Common RAM 0x70-0x7F (the compiler's registers)");
  const std::pair<const char *, const char *> Names[] = {
      {"pc", "PC"},         {"w", "WREG"},     {"status", "STATUS"},
      {"bsr", "BSR"},       {"pclath", "PCLATH"}, {"fsr0", "FSR0"},
      {"fsr1", "FSR1"},     {"stkptr", "STKPTR"},
  };
  for (const auto &[from, to] : Names)
    if (auto *r = find(from)) {
      r->alt_name = ConstString(from);
      r->name = ConstString(to);
      r->set_name = core;
    }
  for (auto &r : regs)
    if (r.set_name != core)
      r.set_name = common;
  // The halves of the pointer registers, which the data sheet lists.
  for (const char *fsr : {"FSR0", "FSR1"}) {
    auto *whole = find(fsr);
    if (!whole || whole->regnum_remote == LLDB_INVALID_REGNUM)
      continue;
    const uint32_t whole_num = whole->regnum_remote;
    const bool first = llvm::StringRef(fsr) == "FSR0";
    for (unsigned half = 0; half != 2; ++half) {
      DynamicRegisterInfo::Register part;
      part.name = ConstString((llvm::Twine(fsr) + (half ? "H" : "L")).str());
      part.set_name = core;
      part.byte_size = 1;
      part.encoding = eEncodingUint;
      part.format = eFormatHex;
      part.regnum_dwarf = (first ? 27 : 29) + half;
      part.value_regs = {whole_num};
      part.invalidate_regs = {whole_num};
      part.value_reg_offset = half;
      regs.push_back(part);
    }
  }

  // The compiler keeps 16-bit values in pairs of its byte registers:
  // rp0 is r1:r0, with DWARF number 16, and so on to rp6.
  for (unsigned pair = 0; pair != 7; ++pair) {
    auto *lo = find(("r" + llvm::Twine(2 * pair)).str());
    auto *hi = find(("r" + llvm::Twine(2 * pair + 1)).str());
    if (!lo || !hi || lo->regnum_remote == LLDB_INVALID_REGNUM ||
        hi->regnum_remote == LLDB_INVALID_REGNUM)
      return;
    DynamicRegisterInfo::Register rp;
    rp.name = ConstString(("rp" + llvm::Twine(pair)).str());
    rp.set_name = ConstString("Compiler 16-bit pairs (rp0 is r1:r0)");
    rp.byte_size = 2;
    rp.encoding = eEncodingUint;
    rp.format = eFormatHex;
    rp.regnum_dwarf = 16 + pair;
    // The uint32_ts are copied out before the vector grows.
    uint32_t lo_num = lo->regnum_remote, hi_num = hi->regnum_remote;
    rp.value_regs = {lo_num, hi_num};
    rp.invalidate_regs = {lo_num, hi_num};
    regs.push_back(rp);
  }
}

// (The stack's base is added with DW_OP_plus_uconst because a constant
// pushed by itself is of the size of a pointer, 16 bits.)
// The call frame address identifies a frame and must be lower the deeper
// the frame is, as on a stack that grows downward: LLDB orders frames by
// it. StackBase + 2 * ((30 - stkptr) & 31); the compiler's .debug_frame
// gives the same.
static const uint8_t g_cfa[] = {
    llvm::dwarf::DW_OP_const1u, 0x1E,
    llvm::dwarf::DW_OP_bregx, dwarf_stkptr, 0,
    llvm::dwarf::DW_OP_minus,
    llvm::dwarf::DW_OP_const1u, 0x1F, llvm::dwarf::DW_OP_and,
    llvm::dwarf::DW_OP_lit1,  llvm::dwarf::DW_OP_shl,
    llvm::dwarf::DW_OP_plus_uconst, 0x80, 0x80, 0x84, 0x04, // 0x810000
};
// The caller's pc: the word address in that entry, as a byte address.
static const uint8_t g_return_pc[] = {
    llvm::dwarf::DW_OP_bregx, dwarf_stkptr, 0,
    llvm::dwarf::DW_OP_lit1,  llvm::dwarf::DW_OP_shl,
    llvm::dwarf::DW_OP_plus_uconst, 0x80, 0x80, 0x84, 0x04, // 0x810000
    llvm::dwarf::DW_OP_deref_size, 2,
    llvm::dwarf::DW_OP_lit1,  llvm::dwarf::DW_OP_shl,
};
// The caller's stkptr: one less, in five bits (0x1F is the empty stack).
static const uint8_t g_caller_stkptr[] = {
    llvm::dwarf::DW_OP_bregx, dwarf_stkptr, 0x7f,
    llvm::dwarf::DW_OP_lit31, llvm::dwarf::DW_OP_and,
};

// A function with a stack frame finds it from its stack level: the value
// the stack pointer cell (the first two bytes of RAM) has while it runs.
// In the innermost frame that is the cell's content; in a caller's it is
// what unwinding gives for FSR1, and the two are told apart by BSR, which
// unwinding sets to 0xFF. The compiler's call frame information says how
// each function with a stack frame changes the level; code without any is
// taken not to change it, which is right for a function with a static
// frame and for the runtime library. (The same rules as in the compiler's
// MCTargetDesc/PIC16F1Dwarf.h.)
static const uint8_t g_caller_mark[] = {llvm::dwarf::DW_OP_const1u, 0xFF};
static const uint8_t g_stack_level[] = {
    llvm::dwarf::DW_OP_bregx, dwarf_bsr, 0,
    llvm::dwarf::DW_OP_const1u, 0xFF, llvm::dwarf::DW_OP_eq,
    llvm::dwarf::DW_OP_bra, 11, 0,
    llvm::dwarf::DW_OP_lit0,
    llvm::dwarf::DW_OP_plus_uconst, 0x80, 0xC0, 0x80, 0x04, // 0x802000
    llvm::dwarf::DW_OP_deref_size, 2,
    llvm::dwarf::DW_OP_skip, 2, 0,
    llvm::dwarf::DW_OP_breg0 + dwarf_fsr1, 0,
};

UnwindPlanSP ABIPIC16F1::CreatePlan(const char *name) {
  UnwindPlan::Row row;
  row.GetCFAValue().SetIsDWARFExpression(g_cfa, sizeof(g_cfa));
  UnwindPlan::Row::AbstractRegisterLocation pc, stkptr;
  pc.SetIsDWARFExpression(g_return_pc, sizeof(g_return_pc));
  stkptr.SetIsDWARFExpression(g_caller_stkptr, sizeof(g_caller_stkptr));
  row.SetRegisterInfo(dwarf_pc, pc);
  row.SetRegisterInfo(dwarf_stkptr, stkptr);
  UnwindPlan::Row::AbstractRegisterLocation mark, level;
  mark.SetIsDWARFExpression(g_caller_mark, sizeof(g_caller_mark));
  level.SetIsDWARFExpression(g_stack_level, sizeof(g_stack_level));
  row.SetRegisterInfo(dwarf_bsr, mark);
  row.SetRegisterInfo(dwarf_fsr1, level);
  // The registers a call preserves (r8-r13 and the pairs made of them) are
  // taken to be what they are now, which is exact unless a function further
  // down has saved one and is using it. Nothing is known of the others.
  // Variables with fixed addresses, where a function with a static frame
  // keeps its own, need no register at all.
  for (uint32_t reg : {8, 9, 10, 11, 12, 13, 20, 21, 22})
    row.SetRegisterLocationToSame(reg, /*must_replace=*/true);
  row.SetUnspecifiedRegistersAreUndefined(true);

  auto plan_sp = std::make_shared<UnwindPlan>(eRegisterKindDWARF);
  plan_sp->AppendRow(std::move(row));
  plan_sp->SetSourceName(name);
  plan_sp->SetSourcedFromCompiler(eLazyBoolNo);
  plan_sp->SetUnwindPlanValidAtAllInstructions(eLazyBoolYes);
  plan_sp->SetUnwindPlanForSignalTrap(eLazyBoolNo);
  return plan_sp;
}

// A call pushes the return address and nothing else, at once, so the same
// plan holds at the first instruction of a function and at every other.
UnwindPlanSP ABIPIC16F1::CreateFunctionEntryUnwindPlan() {
  return CreatePlan("pic16f1 hardware stack at function entry");
}

UnwindPlanSP ABIPIC16F1::CreateDefaultUnwindPlan() {
  return CreatePlan("pic16f1 hardware stack");
}

bool ABIPIC16F1::RegisterIsVolatile(const RegisterInfo *reg_info) {
  // r8-r13 are preserved by a call; everything else is free to change.
  if (!reg_info)
    return true;
  llvm::StringRef name(reg_info->name);
  for (const char *saved : {"r8", "r9", "r10", "r11", "r12", "r13", "rp4",
                            "rp5", "rp6", "stkptr", "pc"})
    if (name.equals_insensitive(saved))
      return false;
  return true;
}

Status ABIPIC16F1::SetReturnValueObject(StackFrameSP &frame_sp,
                                        ValueObjectSP &new_value) {
  return Status::FromErrorString(
      "setting a return value is not supported on PIC16F1");
}
