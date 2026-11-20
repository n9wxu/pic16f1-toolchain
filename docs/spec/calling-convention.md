# Calling convention

Status: draft v1, awaiting approval.

## Registers

The compiler's registers are bytes of common RAM, reachable from every bank.

| Name | File address | Role | Across a call |
|---|---|---|---|
| `R0`-`R7` | 0x70-0x77 | Arguments, results, temporaries | Not preserved |
| `R8`-`R13` | 0x78-0x7D | Temporaries | Preserved by the callee |
| `TMP0`, `TMP1` | 0x7E, 0x7F | Scratch inside one instruction sequence | Not preserved |
| `W` | — | Scratch | Not preserved |
| `FSR0` | 0x04, 0x05 | Scratch pointer | Not preserved |
| `FSR1` | 0x06, 0x07 | Data stack pointer | Preserved |
| `BSR` | 0x08 | Bank select | Not preserved; undefined on entry |
| `PCLATH` | 0x0A | Page select | Not preserved; undefined on entry |
| `STATUS` | 0x03 | Flags | Not preserved |

A 16-bit value occupies an even-numbered register and the next one, low byte
first: `RP0` is `R0`:`R1`, up to `RP6` which is `R12`:`R13`.

Because `PCLATH` is undefined on entry, every `CALL` and `GOTO` is preceded
by a `MOVLP`. Because `BSR` is undefined, every banked access is preceded by
a `MOVLB` unless the same function already selected that bank.

## Types

| C type | Bits | | C type | Bits |
|---|---|---|---|---|
| `char` (unsigned) | 8 | | `long long` | 64 |
| `short`, `int` | 16 | | pointer | 16 |
| `long` | 32 | | `float`, `double`, `long double` | 32 (IEEE single) |

Everything is aligned to one byte. Multi-byte values are little-endian.
A data pointer is an FSR address: it can point at RAM or at flash. A function
pointer is the function's word address.

## Arguments

Arguments are assigned left to right.

1. A value wider than 16 bits is split into 16-bit parts, low part first.
   Each part is then assigned as a separate 16-bit value.
2. An 8-bit value takes the lowest-numbered free register of `R0`-`R7`.
3. A 16-bit value takes the lowest-numbered free pair of `RP0`-`RP3`.
4. A value that finds no register goes on the stack. Stack arguments are
   stored in order at increasing addresses, starting at the address in `FSR1`
   at the moment of the call. An 8-bit argument takes one byte, a 16-bit
   value two.
5. Narrow integers are **not** widened to `int`. The upper bits a callee
   might expect do not exist.
6. A struct or union is passed as a pointer to a copy that the caller makes.
7. In a call to a variadic function every argument, named or not, goes on the
   stack. The usual C promotions apply to the variable arguments.

A later, smaller argument may use a register that an earlier, larger one
skipped. Example: `f(uint8_t a, uint16_t b, uint8_t c)` puts `a` in `R0`,
`b` in `RP1` (`R2`:`R3`) and `c` in `R1`.

The caller owns the stack-argument area; it is part of the caller's frame and
the callee does not remove it.

## Results

| Result | Location |
|---|---|
| 8 bits | `R0` |
| 16 bits | `RP0` |
| 32 bits | `RP0` (low), `RP1` (high) |
| 64 bits | `RP0`-`RP3`, low part first |
| struct, union | Written through a pointer the caller passes as a hidden first argument |

## Functions only the program itself calls

The rules above are what any code outside a compilation can rely on:
another object file, hand-written assembly, the runtime library, a call
through a pointer. A function that none of those can reach (every call of it
is a direct call the compiler can see, which with whole-program compilation
is nearly every function) may be compiled with a variation, chosen for that
function alone and applied to it and to all of its callers together:

| Variation | Instead of |
|---|---|
| The 8-bit result in `W` | `R0` |
| One 8-bit argument in `W` | Its byte register |
| Only the registers the function really changes are lost across a call of it | All of `R0`-`R7` and both FSRs |

A variation is used only where it makes the function and its callers
together smaller or faster and neither worse; see "Arguments and results in
W" in docs/toolchain.md. Nothing else about the function changes: its other
arguments, wider results, the stack and the preserved registers are as
above.

## Runtime library

The compiler calls these with the convention above: `__mulqi3`, `__mulhi3`,
`__mulsi3`, `__muldi3`; `__udivXX3`, `__umodXX3`, `__divXX3`, `__modXX3` for
`XX` in `qi`, `hi`, `si`, `di`; `__ashlsi3`, `__lshrsi3`, `__ashrsi3` and the
`di` forms (value, then a 16-bit count); the single-precision soft-float
routines of compiler-rt; and `memcpy`, `memmove`, `memset`, `memcmp`.

## Open

- Whether to promote narrow arguments for compatibility with code that
  mixes prototyped and unprototyped calls. Today such a mismatch is a bug in
  the program.
- The Rust and Zig front ends must implement these rules themselves and be
  checked against Clang with cross-language tests (plan section 2).
