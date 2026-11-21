# The F1 Evaluation Platform demo, for the PIC16LF1937 on that board.
#
#   make            build build/f1-eval.hex (and .elf) with debug information
#   make size       what the program takes of the device
#   make list       the program's disassembly
#   make flash      program the board through the probe's OpenOCD
#                   (OPENOCD=its directory, the one with src/openocd and tcl)
#   make clean
#
# DEBUGGER=1 builds for an in-circuit debugger on the part itself: the
# program then keeps clear of the debugger's RAM and its two pins, and
# stays awake.

TOOLCHAIN ?= ../../toolchain/bin
DEVICE    ?= pic16f1937
OPT       ?= -Os
CFLAGS    ?= -std=c++23
NAME      := f1-eval
SOURCES   := $(wildcard *.cpp)
VDD       ?= 3300

CC    := $(TOOLCHAIN)/pic16f1-cc
SIM   := $(TOOLCHAIN)/pic16f1-sim
FLAGS := -g $(OPT) -mcpu=$(DEVICE) -Wall -Wextra $(if $(DEBUGGER),-mdebugger) $(CFLAGS)

build/$(NAME).hex: $(SOURCES) $(wildcard *.hpp) Makefile
	@mkdir -p build
	$(CC) $(FLAGS) -o $@ $(SOURCES)
	@echo "$$($(SIM) size --device $(DEVICE) build/$(NAME).elf) words of flash used"

.PHONY: size list flash clean
size: build/$(NAME).hex
	@echo "$$($(SIM) size --device $(DEVICE) build/$(NAME).elf) words of flash used"

list: build/$(NAME).hex
	@$(SIM) disasm --device $(DEVICE) build/$(NAME).elf

flash: build/$(NAME).hex
	@test -n "$(OPENOCD)" || { echo "make flash OPENOCD=<the probe's OpenOCD directory>"; exit 1; }
	cd "$(OPENOCD)" && src/openocd -s tcl -f interface/pk2icsp.cfg -c "pk2icsp vdd $(VDD)" -c "pk2icsp hvp on" \
	    -f target/pic16f193x.cfg -c "init; flash write_image erase $(abspath build/$(NAME).hex); shutdown"

clean:
	rm -rf build
