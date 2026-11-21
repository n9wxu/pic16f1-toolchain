# @NAME@, an Arduino sketch for the @DEVICE_UPPER@. Made by pic16f1-new-project.
#
#   make            build build/@NAME@.hex (and .elf) with debug information
#   make sim        start the simulator and wait for a debugger on port 3333
#   make debug      debug on the simulator with LLDB in this terminal
#   make size       what the program takes of the device
#   make list       the program's disassembly
#   make clean
#
# DEBUGGER=1 builds for an in-circuit debugger on the part itself.
# pic16f1-build does the work (the sketch's .ino files made into C++, the
# Arduino core and the libraries the sketch includes compiled and linked)
# and reads DEVICE, OPT and F_CPU from here when it is run without make.

TOOLCHAIN ?= @TOOLCHAIN@
DEVICE    ?= @DEVICE@
OPT       ?= -Os
F_CPU     ?= 16000000
# fast: multiplication, division and floating point in a half to a third
# of the time, for some hundreds of words more.
MATH      ?= small
NAME      := @NAME@

BUILD := $(TOOLCHAIN)/pic16f1-build --device $(DEVICE) --opt $(OPT) --f-cpu $(F_CPU) --math $(MATH) --name $(NAME) $(if $(DEBUGGER),--debugger)

.PHONY: all sim debug size list clean
all:
	@$(BUILD) build
sim debug size list clean:
	@$(BUILD) $@
