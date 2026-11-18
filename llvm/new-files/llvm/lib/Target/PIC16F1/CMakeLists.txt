add_llvm_component_group(PIC16F1)

set(LLVM_TARGET_DEFINITIONS PIC16F1.td)

tablegen(LLVM PIC16F1GenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM PIC16F1GenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM PIC16F1GenCallingConv.inc -gen-callingconv)
tablegen(LLVM PIC16F1GenDAGISel.inc -gen-dag-isel)
tablegen(LLVM PIC16F1GenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM PIC16F1GenInstrInfo.inc -gen-instr-info)
tablegen(LLVM PIC16F1GenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM PIC16F1GenRegisterInfo.inc -gen-register-info)
tablegen(LLVM PIC16F1GenSDNodeInfo.inc -gen-sd-node-info)
tablegen(LLVM PIC16F1GenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(PIC16F1CommonTableGen)

add_llvm_target(PIC16F1CodeGen
  PIC16F1AsmPrinter.cpp
  PIC16F1ExpandPseudo.cpp
  PIC16F1FrameLowering.cpp
  PIC16F1ISelDAGToDAG.cpp
  PIC16F1ISelLowering.cpp
  PIC16F1InstrInfo.cpp
  PIC16F1MachineFunctionInfo.cpp
  PIC16F1RegisterInfo.cpp
  PIC16F1SelectionDAGInfo.cpp
  PIC16F1Subtarget.cpp
  PIC16F1TargetMachine.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
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
