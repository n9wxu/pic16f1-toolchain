//===-- PIC16F1InstrInfo.h - Instruction information ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PIC16F1_PIC16F1INSTRINFO_H
#define LLVM_LIB_TARGET_PIC16F1_PIC16F1INSTRINFO_H

#include "PIC16F1RegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "PIC16F1GenInstrInfo.inc"

namespace llvm {

class PIC16F1Subtarget;

/// One machine instruction of an expansion, before it is built.
struct PIC16F1RawInst {
  unsigned Opc;
  int64_t Op0 = 0;
  int64_t Op1 = 0;
  unsigned NumOps = 0;
  /// The single operand is the branch target of the pseudo being expanded.
  bool IsBranch = false;
};

class PIC16F1InstrInfo : public PIC16F1GenInstrInfo {
  const PIC16F1RegisterInfo RI;
  virtual void anchor();

public:
  explicit PIC16F1InstrInfo(const PIC16F1Subtarget &STI);

  const PIC16F1RegisterInfo &getRegisterInfo() const { return RI; }

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                   const DebugLoc &DL, Register DestReg, Register SrcReg,
                   bool KillSrc, bool RenamableDest = false,
                   bool RenamableSrc = false) const override;

  void storeRegToStackSlot(
      MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
      bool isKill, int FrameIndex, const TargetRegisterClass *RC, Register VReg,
      MachineInstr::MIFlag Flags = MachineInstr::NoFlags) const override;
  void loadRegFromStackSlot(
      MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register DestReg,
      int FrameIdx, const TargetRegisterClass *RC, Register VReg,
      unsigned SubReg = 0,
      MachineInstr::MIFlag Flags = MachineInstr::NoFlags) const override;

  /// Recognising plain stack-slot loads and stores lets the generic passes
  /// remove a reload that is immediately stored back to the same slot.
  Register isLoadFromStackSlot(const MachineInstr &MI,
                               int &FrameIndex) const override;
  Register isStoreToStackSlot(const MachineInstr &MI,
                              int &FrameIndex) const override;

  unsigned getInstSizeInBytes(const MachineInstr &MI) const override;

  bool
  reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const override;
  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify) const override;
  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;
  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;
  MachineBasicBlock *getBranchDestBlock(const MachineInstr &MI) const override;
  bool isBranchOffsetInRange(unsigned BranchOpc,
                             int64_t BrOffset) const override;
  void insertIndirectBranch(MachineBasicBlock &MBB,
                            MachineBasicBlock &NewDestBB,
                            MachineBasicBlock &RestoreBB, const DebugLoc &DL,
                            int64_t BrOffset, RegScavenger *RS) const override;

  /// The machine instructions for a compare-and-branch pseudo. The last one,
  /// if any, is the BRA to the target.
  void buildCompareBranch(const MachineInstr &MI,
                          SmallVectorImpl<PIC16F1RawInst> &Seq) const;

  static bool isCompareBranch(unsigned Opc);

  // Machine outliner: instruction sequences that occur several times in a
  // module become one subroutine. A call uses only the hardware return
  // stack, so sequences that use the software stack are as good as any.
  bool shouldOutlineFromFunctionByDefault(MachineFunction &MF) const override;
  bool isFunctionSafeToOutlineFrom(MachineFunction &MF,
                                   bool OutlineFromLinkOnceODRs) const override;
  std::optional<std::unique_ptr<outliner::OutlinedFunction>>
  getOutliningCandidateInfo(
      const MachineModuleInfo &MMI,
      std::vector<outliner::Candidate> &RepeatedSequenceLocs,
      unsigned MinRepeats) const override;
  outliner::InstrType getOutliningTypeImpl(const MachineModuleInfo &MMI,
                                           MachineBasicBlock::iterator &MIT,
                                           unsigned Flags) const override;
  void buildOutlinedFrame(MachineBasicBlock &MBB, MachineFunction &MF,
                          const outliner::OutlinedFunction &OF) const override;
  MachineBasicBlock::iterator
  insertOutlinedCall(Module &M, MachineBasicBlock &MBB,
                     MachineBasicBlock::iterator &It, MachineFunction &MF,
                     outliner::Candidate &C) const override;

  /// True if byte \p Off of \p GV is sure to be in the same bank as its
  /// first byte. The linker (pic16f1KeepInBank in lld) keeps a section of
  /// up to 16 bytes within one bank, and every variable has its own section
  /// unless the program names one.
  static bool inOneBank(const GlobalValue *GV, int64_t Off);

  /// True if \p MI makes the instruction after it conditional.
  static bool isSkip(const MachineInstr &MI);

  /// File address of a byte register, or of the low byte of a pair.
  unsigned fileAddr(Register Reg) const { return RI.getEncodingValue(Reg); }
};

} // namespace llvm

#endif
