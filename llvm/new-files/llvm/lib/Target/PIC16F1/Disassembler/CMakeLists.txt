add_llvm_component_library(LLVMPIC16F1Disassembler
  PIC16F1Disassembler.cpp

  LINK_COMPONENTS
  MCDisassembler
  MC
  PIC16F1Info
  Support

  ADD_TO_COMPONENT
  PIC16F1
  )
