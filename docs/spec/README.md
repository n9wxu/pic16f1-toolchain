# Specifications

Version 1 drafts. Each describes what the toolchain implements today and is
the reference that tests are written against. They need the architect's
approval; until then they can change. After approval, a change to one of
these needs approval too (operating rule 1 in [the plan](../plan.md)).

| Specification | Covers | Status |
|---|---|---|
| [Calling convention](calling-convention.md) | Registers, arguments, results, types | Draft, awaiting approval |
| [Stack and interrupts](stack-and-interrupts.md) | Data stack, frames, interrupt handlers, startup | Draft, awaiting approval |
| [Object format](object-format.md) | ELF, address spaces, relocations, HEX conversion | Draft, awaiting approval |
| [Assembly syntax](assembly-syntax.md) | Mnemonics, operands, address-part operators | Draft, awaiting approval |
| [Simulator interface](simulator-interface.md) | Program exit, output, images, result record | Draft, awaiting approval |
| [Debug interface](debug-interface.md) | Debugger address space, registers, gdb-remote packets, call stack, variable locations | Draft, awaiting approval |

Instruction semantics are specified by Microchip's data sheets (for example
DS40001364 for the PIC16(L)F193x). The simulator's instruction tests cite
them, and the assembler is checked against Microchip's `pic-as`.

Provisional choices that need a decision are marked **Open** in each file.
