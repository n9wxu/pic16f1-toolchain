# Assembly syntax

Status: draft v1, awaiting approval.

The syntax is Microchip's where one exists, in GNU assembler dress
(directives such as `.section`, `.globl`, `.byte`, `.2byte`; `;` starts a
comment). Mnemonics and the keywords `W`, `F`, `FSR0`, `FSR1` are not case
sensitive. The compiler prints mnemonics in lower case.

## Instructions

| Form | Example |
|---|---|
| Byte operation: file, destination | `addwf 0x25, F` (destination `W`, `F`, `0` or `1`) |
| File only | `movwf 0x70`, `clrf 0x71` |
| Bit operation: file, bit | `bsf 0x0B, 7` |
| Literal | `movlw 0xA5`, `retlw 3` |
| Control | `call label`, `goto label`, `bra label`, `callw`, `brw`, `return`, `retfie` |
| Bank and page | `movlb 3`, `movlp 0x08` |
| FSR | `addfsr FSR1, -4` |
| Indirect with increment | `moviw ++FSR0`, `moviw --FSR0`, `moviw FSR0++`, `moviw FSR0--` (also `movwi`) |
| Indirect with offset | `moviw 3[FSR1]`, `movwi -2[FSR0]` (offset -32 to 31) |
| No operand | `nop`, `sleep`, `clrwdt`, `reset`, `clrw`, `option` |

A file operand is the 7-bit address within the selected bank. A numeric
operand of `bra` is the raw word offset.

`movlb` assembles to the 5-bit or the 6-bit encoding according to the
device selected with `-mcpu`.

## Address-part operators

| Operator | Value | Typical use |
|---|---|---|
| `lo8(x)`, `hi8(x)` | Low and high byte of a data address | `movlw lo8(buffer)` |
| `pm_lo8(x)`, `pm_hi8(x)` | Low and high byte of a code word address | `movlw pm_hi8(handler)` |
| `pm(x)` | Code word address, in data | `.2byte pm(handler)` |
| `bank(x)` | Bank of a data address | `movlb bank(counter)` |
| `off(x)` | File address of a data address within its bank | `movf off(counter), W` |
| `page(x)` | `PCLATH` value for a code address | `movlp page(func)` |

A symbolic file operand without an operator means `off()`; a symbolic
`movlb` operand means `bank()`; a symbolic `movlp` operand means `page()`.

A symbol named like a keyword (`W`, `F`, `FSR0`, `FSR1`) must be written in
parentheses, and the compiler prints it that way.

## Verification

Encodings are compared with Microchip `pic-as` output, and every instruction
form is assembled, disassembled and reassembled to identical bytes
(`scripts/test-assembler.sh`).

## Open

- Names for the compiler's registers (`R0`...) and the core registers in
  assembly. Today file operands print as numbers.
- Whether to accept MPASM-style directives (`banksel`, `pagesel`).
