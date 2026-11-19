add_llvm_component_group(PIC16F1)

set(LLVM_TARGET_DEFINITIONS PIC16F1.td)

tablegen(LLVM PIC16F1GenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM PIC16F1GenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM PIC16F1GenCallingConv.inc -gen-callingconv)
tablegen(LLVM PIC16F1GenDAGISel.inc -gen-dag-isel)
tablegen(LLVM PIC16F1GenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM PIC16F1GenInstrInfo.inc -gen-instr-info)
tablegen(LLVM PIC16F1GenPreLegalizeGICombiner.inc -gen-global-isel-combiner
              -combiners="PIC16F1PreLegalizerCombiner")
tablegen(LLVM PIC16F1GenPostLegalizeGICombiner.inc -gen-global-isel-combiner
              -combiners="PIC16F1PostLegalizerCombiner")
tablegen(LLVM PIC16F1GenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM PIC16F1GenRegisterBank.inc -gen-register-bank)
tablegen(LLVM PIC16F1GenRegisterInfo.inc -gen-register-info)
tablegen(LLVM PIC16F1GenSDNodeInfo.inc -gen-sd-node-info)
tablegen(LLVM PIC16F1GenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(PIC16F1CommonTableGen)

add_llvm_target(PIC16F1CodeGen
  GISel/PIC16F1CallLowering.cpp
  GISel/PIC16F1InstructionSelector.cpp
  GISel/PIC16F1LegalizerInfo.cpp
  GISel/PIC16F1PostLegalizerCombiner.cpp
  GISel/PIC16F1PreLegalizerCombiner.cpp
  GISel/PIC16F1RegisterBankInfo.cpp
  PIC16F1AsmPrinter.cpp
  PIC16F1ExpandPseudo.cpp
  PIC16F1Peephole.cpp
  PIC16F1PointerSetup.cpp
  PIC16F1FrameLowering.cpp
  PIC16F1ISelDAGToDAG.cpp
  PIC16F1ISelLowering.cpp
  PIC16F1InstrInfo.cpp
  PIC16F1MachineFunctionInfo.cpp
  PIC16F1RegisterInfo.cpp
  PIC16F1SelectionDAGInfo.cpp
  PIC16F1StaticFrames.cpp
  PIC16F1Subtarget.cpp
  PIC16F1TargetMachine.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  GlobalISel
  MC
  PIC16F1Desc
  PIC16F1Info
  SelectionDAG
  Support
  Target
  TargetParser
  TransformUtils

  ADD_TO_COMPONENT
  PIC16F1
  )

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
