add_lldb_library(lldbPluginABIPIC16F1 PLUGIN
  ABIPIC16F1.cpp

  LINK_COMPONENTS
    Support
    TargetParser
  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
    lldbValueObject
  )
