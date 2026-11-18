add_llvm_component_library(LLVMPIC16F1Info
  PIC16F1TargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  PIC16F1
  )
