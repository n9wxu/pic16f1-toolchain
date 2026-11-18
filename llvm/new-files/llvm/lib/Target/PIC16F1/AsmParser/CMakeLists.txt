add_llvm_component_library(LLVMPIC16F1AsmParser
  PIC16F1AsmParser.cpp

  LINK_COMPONENTS
  CodeGenTypes
  MC
  MCParser
  PIC16F1Desc
  PIC16F1Info
  Support

  ADD_TO_COMPONENT
  PIC16F1
  )
