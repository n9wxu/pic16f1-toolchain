add_llvm_component_library(LLVMPIC16F1Desc
  PIC16F1AsmBackend.cpp
  PIC16F1ELFObjectWriter.cpp
  PIC16F1InstPrinter.cpp
  PIC16F1MCAsmInfo.cpp
  PIC16F1MCCodeEmitter.cpp
  PIC16F1MCTargetDesc.cpp

  LINK_COMPONENTS
  MC
  PIC16F1Info
  Support
  TargetParser

  ADD_TO_COMPONENT
  PIC16F1
  )
