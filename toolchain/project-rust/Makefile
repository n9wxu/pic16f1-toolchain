# @NAME@, for the @DEVICE_UPPER@, in Rust. Made by pic16f1-new-project.
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
# cargo does the compiling: rustc for a stand-in target, and the
# toolchain's pic16f1-rust-ld as its linker, which makes the PIC16F1
# program. What the linker said (the hardware stack count, for one) is in
# build/@NAME@.link.txt. Needs rustup; rust-toolchain.toml names the
# compiler, which rustup fetches the first time.

TOOLCHAIN ?= @TOOLCHAIN@
RUST      ?= @RUST@
DEVICE    ?= @DEVICE@
OPT       ?= -Os
NAME      := @NAME@

SIM   := $(TOOLCHAIN)/pic16f1-sim
LLDB  := $(TOOLCHAIN)/pic16f1-lldb
LINK  := -mcpu=$(DEVICE) $(OPT) -g -mstack-report $(if $(DEBUGGER),-mdebugger)
export CARGO_TARGET_PIC16F1_LINKER := $(TOOLCHAIN)/pic16f1-rust-ld
export CARGO_TARGET_PIC16F1_RUSTFLAGS := -C linker-plugin-lto $(foreach a,$(LINK),-C link-arg=$(a))

build/$(NAME).hex: $(wildcard src/*.rs) Cargo.toml Makefile
	cargo build --release -Zbuild-std=core --target $(RUST)/pic16f1.json
	@mkdir -p build
	@cp target/pic16f1/release/$(NAME).elf build/$(NAME).elf
	@cp "$$(ls -t target/pic16f1/release/deps/*.link.txt | head -1)" build/$(NAME).link.txt
	@$(SIM) elf2hex build/$(NAME).elf build/$(NAME).hex
	@cat build/$(NAME).link.txt
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
	rm -rf build target
