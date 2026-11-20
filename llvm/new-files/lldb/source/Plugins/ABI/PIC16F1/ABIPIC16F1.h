//===-- ABIPIC16F1.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The Microchip enhanced mid-range core (PIC16F1xxx).
//
// Addresses are those of the ELF file, which a gdb-remote stub for the
// target serves:
//   0x000000 up   program memory in bytes, two to a 14-bit word
//   0x800000 up   data memory as an FSR addresses it (a C data pointer p is
//                 the address 0x800000 + p)
//   0x810000 up   the hardware return stack, two bytes per level, each a
//                 word address
//
// Return addresses are only on that hardware stack, so the call stack is
// unwound from it: the frame at stack level n returns to the address in
// entry n, and its caller is at level n - 1.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_ABI_PIC16F1_ABIPIC16F1_H
#define LLDB_SOURCE_PLUGINS_ABI_PIC16F1_ABIPIC16F1_H

#include "lldb/Target/ABI.h"
#include "lldb/lldb-private.h"

class ABIPIC16F1 : public lldb_private::MCBasedABI {
public:
  ~ABIPIC16F1() override = default;

  size_t GetRedZoneSize() const override { return 0; }

  /// The scratch stack an expression is interpreted with. It has to fit in
  /// the addresses a 16-bit pointer can hold that are not memory of the
  /// target's (see IRMemoryMap::FindSpace).
  uint64_t GetStackFrameSize() override { return 2048; }

  bool PrepareTrivialCall(lldb_private::Thread &thread, lldb::addr_t sp,
                          lldb::addr_t functionAddress,
                          lldb::addr_t returnAddress,
                          llvm::ArrayRef<lldb::addr_t> args) const override {
    return false;
  }

  bool GetArgumentValues(lldb_private::Thread &thread,
                         lldb_private::ValueList &values) const override {
    return false;
  }

  lldb_private::Status
  SetReturnValueObject(lldb::StackFrameSP &frame_sp,
                       lldb::ValueObjectSP &new_value) override;

  lldb::ValueObjectSP
  GetReturnValueObjectImpl(lldb_private::Thread &thread,
                           lldb_private::CompilerType &type) const override {
    return lldb::ValueObjectSP();
  }

  lldb::UnwindPlanSP CreateFunctionEntryUnwindPlan() override;
  lldb::UnwindPlanSP CreateDefaultUnwindPlan() override;

  bool RegisterIsVolatile(const lldb_private::RegisterInfo *reg_info) override;

  /// The call frame address is the address of a hardware stack entry.
  bool CallFrameAddressIsValid(lldb::addr_t cfa) override {
    return cfa >= StackBase && cfa < StackBase + 2 * 32;
  }

  bool CodeAddressIsValid(lldb::addr_t pc) override {
    return pc < DataBase && (pc & 1) == 0;
  }

  /// A C data pointer is 16 bits; data memory is at DataBase in the address
  /// space the debugger works in.
  lldb::addr_t FixDataAddress(lldb::addr_t addr) override {
    return addr < 0x10000 ? addr + DataBase : addr;
  }
  lldb::addr_t FixAnyAddress(lldb::addr_t addr) override { return addr; }

  void AugmentRegisterInfo(
      std::vector<lldb_private::DynamicRegisterInfo::Register> &regs) override;

  static const lldb::addr_t DataBase = 0x800000;
  static const lldb::addr_t StackBase = 0x810000;

  static void Initialize();
  static void Terminate();
  static lldb::ABISP CreateInstance(lldb::ProcessSP process_sp,
                                    const lldb_private::ArchSpec &arch);
  static llvm::StringRef GetPluginNameStatic() { return "pic16f1"; }
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

protected:
  uint32_t GetGenericNum(llvm::StringRef reg) override;

private:
  using lldb_private::MCBasedABI::MCBasedABI;
  lldb::UnwindPlanSP CreatePlan(const char *name);
};

#endif // LLDB_SOURCE_PLUGINS_ABI_PIC16F1_ABIPIC16F1_H
