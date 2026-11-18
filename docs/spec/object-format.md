# Object format

Status: draft v1, awaiting approval.

## ELF

ELF32, little-endian, OS/ABI "standalone". Machine number `0x5046`.

**Open:** `0x5046` is provisional and unregistered. The plan calls for
requesting an official value.

## Address spaces

Symbol values and section addresses use two ranges.

| Range | Meaning |
|---|---|
| `0x000000`-`0x7FFFFF` | Program memory as byte addresses: two per 14-bit word, low byte first. Word `n` is at byte `2n`. These are the addresses Microchip HEX files use, so configuration words are at `0x1000E` up |
| `0x800000`-`0x80FFFF` | Data: `0x800000` plus the 16-bit address the FSR registers use |

Inside the data range:

| FSR address | Content |
|---|---|
| `0x0000`-`0x0FFF` (`0x1FFF` on 64-bank devices) | Banked registers and RAM, 128 bytes per bank |
| `0x2000` up | Linear RAM: the 80 general purpose bytes of each bank, back to back |
| `0x8000` up | Flash, readable through an FSR: the low 8 bits of word `address - 0x8000` |

Constants (`.rodata`) are placed in the flash range directly after the code,
one byte per program word. `.data` is in linear RAM with its initial values
in the flash range. A data pointer's value is the symbol's address with the
`0x800000` removed.

## Sections

| Section | Placement |
|---|---|
| `.reset` | Word 0 |
| `.isr` | Word 4 |
| `.text` | After `.isr` |
| `.rodata` | Flash data range, after the code |
| `.data` | Linear RAM; load address in the flash data range |
| `.bss` | Linear RAM, cleared at startup |
| `.config` | Configuration words, 16 bits each |

## Relocations

`S` is the symbol's address, `A` the addend, `P` the place. "Banked form" of
a data address: unchanged if below `0x2000`; for linear offset `n` it is bank
`n / 80`, file address `0x20 + n % 80`.

| Number | Name | Patches | Value |
|---|---|---|---|
| 0 | `R_PIC16F1_NONE` | — | — |
| 1 | `R_PIC16F1_8` | 8 bits of data | `S + A` |
| 2 | `R_PIC16F1_16` | 16 bits of data | `S + A` |
| 3 | `R_PIC16F1_32` | 32 bits of data | `S + A` |
| 4 | `R_PIC16F1_PM16` | 16 bits of data | `(S + A) / 2` |
| 5 | `R_PIC16F1_LO8` | bits 7-0 of an instruction | `S + A` |
| 6 | `R_PIC16F1_HI8` | bits 7-0 | `(S + A) >> 8` |
| 7 | `R_PIC16F1_PM_LO8` | bits 7-0 | `(S + A) / 2` |
| 8 | `R_PIC16F1_PM_HI8` | bits 7-0 | `(S + A) / 2 >> 8` |
| 9 | `R_PIC16F1_CALL11` | bits 10-0 (`CALL`, `GOTO`) | `(S + A) / 2` |
| 10 | `R_PIC16F1_PAGE7` | bits 6-0 (`MOVLP`) | `(S + A) / 2 >> 8` |
| 11 | `R_PIC16F1_BRA9` | bits 8-0 (`BRA`) | `(S + A - P - 2) / 2`, signed; error if out of range |
| 12 | `R_PIC16F1_BANK5` | bits 4-0 (`MOVLB`) | bank of the banked form; error above bank 31 |
| 13 | `R_PIC16F1_F7` | bits 6-0 (file operand) | file address of the banked form |
| 14 | `R_PIC16F1_BANK6` | bits 5-0 (`MOVLB`, 64-bank devices) | bank of the banked form |

Values are truncated to the field. `BANK5`, `BANK6` and `F7` are errors for
an address in flash. All relocations use explicit addends (RELA).

## HEX conversion

`pic16f1-sim elf2hex` writes Intel HEX from the loadable segments, by load
address:

- below `0x800000`: bytes as they are;
- `0x808000` up: each byte becomes the program word `0x3400 | byte` (a
  `RETLW`) at word address `load address - 0x808000`;
- other data addresses (RAM) are not written.

## Open

- LTO: section and symbol conventions for bitcode objects.
- Debug information and its relocations.
- Linker relaxation (removing `MOVLB`/`MOVLP` the linker can prove unneeded).
