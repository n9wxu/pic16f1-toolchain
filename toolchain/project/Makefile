# @NAME@, for the @DEVICE_UPPER@. Made by pic16f1-new-project.
#
#   make            build build/@NAME@.hex (and .elf) with debug information
#   make sim        start the simulator and wait for a debugger on port 3333
#   make debug      debug on the simulator with LLDB in this terminal
#   make size       what the program takes of the device
#   make list       the program's disassembly
#   make clean
#
# DEBUGGER=1 builds for an in-circuit debugger on the part itself: the
# program then keeps clear of the debugger's RAM and its two pins.

TOOLCHAIN ?= @TOOLCHAIN@
DEVICE    ?= @DEVICE@
OPT       ?= -O1
NAME      := @NAME@
SOURCES   := $(wildcard *.c)

CC    := $(TOOLCHAIN)/pic16f1-cc
SIM   := $(TOOLCHAIN)/pic16f1-sim
LLDB  := $(TOOLCHAIN)/pic16f1-lldb
FLAGS := -g $(OPT) -mcpu=$(DEVICE) -Wall $(if $(DEBUGGER),-mdebugger) $(CFLAGS)

build/$(NAME).hex: $(SOURCES) $(wildcard *.h) Makefile
	@mkdir -p build
	$(CC) $(FLAGS) -o $@ $(SOURCES)
	@$(SIM) size --device $(DEVICE) build/$(NAME).elf

.PHONY: sim debug size list clean
sim: build/$(NAME).hex
	$(SIM) gdb --device $(DEVICE) --port 3333 build/$(NAME).elf

debug: build/$(NAME).hex
	$(SIM) gdb --device $(DEVICE) --port 3333 build/$(NAME).elf & \
	sleep 0.3; $(LLDB) build/$(NAME).elf -o "gdb-remote 3333" \
	    -o "breakpoint set --one-shot true --name main" -o "continue"

size: build/$(NAME).hex
	@$(SIM) size --device $(DEVICE) build/$(NAME).elf

list: build/$(NAME).hex
	@$(SIM) disasm --device $(DEVICE) build/$(NAME).elf

clean:
	rm -rf build
