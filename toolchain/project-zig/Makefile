# @NAME@, for the @DEVICE_UPPER@, in Zig. Made by pic16f1-new-project.
#
#   make            build build/@NAME@.hex (and .elf) with debug information
#   make sim        start the simulator and wait for a debugger on port 3333
#   make debug      debug on the simulator with LLDB in this terminal
#   make size       what the program takes of the device
#   make list       the program's disassembly
#   make clean
#
# DEBUGGER=1 builds for an in-circuit debugger on the part itself: the
# program then keeps clear of the debugger's RAM.
#
# main.zig is the program's root and imports the other .zig files; C files
# in this directory are compiled into the program too. MODE is Zig's build
# mode (ReleaseSmall, ReleaseFast, ReleaseSafe or Debug); OPT, if given, is
# the code generator's level instead of the one that goes with the mode.
# The Zig compiler is $PIC16F1_ZIG or zig on the PATH.

TOOLCHAIN ?= @TOOLCHAIN@
DEVICE    ?= @DEVICE@
MODE      ?= ReleaseSmall
NAME      := @NAME@

ZIGCC := $(TOOLCHAIN)/pic16f1-zig
SIM   := $(TOOLCHAIN)/pic16f1-sim
LLDB  := $(TOOLCHAIN)/pic16f1-lldb
FLAGS := -g -O $(MODE) $(OPT) -mcpu=$(DEVICE) $(if $(DEBUGGER),-mdebugger) $(CFLAGS)

build/$(NAME).hex: $(wildcard *.zig *.c *.h) Makefile
	@mkdir -p build
	$(ZIGCC) $(FLAGS) -o $@ main.zig $(wildcard *.c)
	@echo "$$($(SIM) size --device $(DEVICE) build/$(NAME).elf) words of flash used"

.PHONY: sim debug size list clean
sim: build/$(NAME).hex
	$(SIM) gdb --device $(DEVICE) --port 3333 build/$(NAME).elf

debug: build/$(NAME).hex
	$(SIM) gdb --device $(DEVICE) --port 3333 build/$(NAME).elf & \
	sleep 0.3; $(LLDB) build/$(NAME).elf -o "gdb-remote 3333" \
	    -o "breakpoint set --one-shot true --name main" -o "continue"

size: build/$(NAME).hex
	@echo "$$($(SIM) size --device $(DEVICE) build/$(NAME).elf) words of flash used"

list: build/$(NAME).hex
	@$(SIM) disasm --device $(DEVICE) build/$(NAME).elf

clean:
	rm -rf build .zig-cache
