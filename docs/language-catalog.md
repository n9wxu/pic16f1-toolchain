# Languages for the PIC16F1: a catalog

Which languages could be used on this processor, by what route, how well
each fits, and what is recommended. Five are implemented and described in
[toolchain.md](toolchain.md) and [languages.md](languages.md); the rest
are assessed here and not built.

## What "fit" is measured against

A PIC16F1 has 2K to 32K words of flash, 128 bytes to 4K of RAM, a
hardware return stack of 16 levels that a program cannot reach into, one
working register, banked RAM, and no operating system. A language fits to
the degree that

- its programs need **no heap** and no garbage collector;
- its run-time support is **small or nothing** (hundreds of words, not
  tens of thousands);
- it does not need **deep or unbounded nesting** of calls, or lets a tool
  bound it;
- its data is **statically sized**, so that variables can have fixed
  addresses;
- it can say **bit, register and interrupt** directly;
- there is a **route** to this target that does not mean writing a second
  compiler: C as an intermediate, LLVM IR through the retargeting of
  [languages.md](languages.md), or a translator small enough to write.

The scores are 0 to 10 and are a judgement, made against those points.
For the five that are built they are backed by measurements; for the rest
they are estimates from how the language and its usual implementations
work, and nothing below the line was compiled for the part.

## Built

| Language | Route | Fit | What was found |
|---|---|---|---|
| C | Clang and a code generator written for the target | 10 | Every benchmark no larger and no slower than XC8's; 0.73 of its size at `-Os` |
| C++ (no exceptions, no RTTI) | The same compiler | 9 | Templates and `constexpr` cost nothing; virtual calls cost a call through a pointer and two bytes an object. The standard library's headers work, but its sort and its string are thousands of words |
| Zig | Its compiler for a stand-in target, LLVM IR retargeted | 8 | Comes closest to C in spirit and in code: packed structs are the device's registers, `comptime` replaces the preprocessor, no hidden run-time. Most of its standard library is for larger machines |
| Rust (`core` only) | `rustc` for a stand-in target, LLVM IR retargeted | 7 | The language works in full and its code is as small as C++'s; `cargo` builds the program. Ownership has little to check where everything is a static or a register, formatting costs 2,800 words, and every possible panic costs a few words and a stack level |
| Forth | A cross-compiler written for the target | 9 | Fits the machine as no other language does: two stacks, no frames, no heap, the hardware named directly. About twice C's size for the same work |

## Recommended next

| Language | Route | Fit | Recommendation |
|---|---|---|---|
| BASIC (compiled, in the manner of PICBASIC or Great Cow BASIC) | A translator to C, about the size of the Forth compiler | 8 | **Build next, if a beginner's language is wanted.** Statically sized variables, no heap, `GOSUB` with bounded nesting, and statements for pins, delays and serial I/O that map onto the device libraries already here. The value is approachability, not capability: it can do nothing C cannot |
| Ladder logic and the IEC 61131-3 languages (structured text, instruction list) | A translator to C; LDmicro already compiles ladder for the PIC16 by itself | 8 | **Worth building for control work.** A scan cycle over fixed variables is exactly what this processor does well, and the result is bounded in time and memory by construction |
| State machine and synchronous languages (Ragel, SCXML compilers, Lustre and Heptagon, Esterel, Céu) | They generate C | 8 | **Use as they are.** Their output is C with static memory and bounded execution, which this toolchain compiles; nothing has to be built except, where a run-time file is needed, a port of a page of C. Lustre-style languages give a proof of bounded memory that C cannot |
| Copilot, Ivory, Atom (Haskell-embedded) | They generate C | 7 | **Use as they are**, for the same reason: constant-memory C. The host toolchain is heavy; the generated code is not |
| JAL ("Just Another Language") | Its own compiler, jalv2, targets the PIC16 directly | 8 | **Nothing to do here.** It was designed for this family and has its own toolchain and library (Jallib). A front end for this code generator would duplicate it |
| An Arduino-style API (`pinMode`, `digitalWrite`, `Serial`) | A header over `<pic/pic.hpp>` | 8 | **Cheap and worth having** for people who know that API. It is a library, not a language: C++ already works |

## Possible, with reservations

| Language | Route | Fit | Recommendation |
|---|---|---|---|
| Nim | Compiles to C (`--os:standalone --mm:none`) | 6 | Likely to work, since its C output is plain; the standard library assumes a heap, and what is left without one is close to C with different spelling. Try it only if someone wants Nim specifically |
| Oberon-07, Pascal | Oberon to C (OBNC, Ofront+); Pascal has no maintained route | 6 | Good languages for the job (static, simple, no hidden costs) with no user base on this processor. Oberon through C is a week's work for the run-time file |
| D (`-betterC`) | LDC for the AVR, LLVM IR retargeted as for Rust and Zig | 6 | Should work by the route already built: `-betterC` has no run-time. Templates and compile-time evaluation, as C++ and Zig have. No reason to prefer it to those two |
| Ada and SPARK | GNAT's LLVM back end, IR retargeted | 5 | The language suits small systems (ranges, representation clauses, no heap required) and SPARK proves absence of run-time errors. The route is long: GNAT-LLVM is hard to build and has no 8-bit target to stand in |
| Swift (Embedded Swift) | Its compiler for the AVR, LLVM IR retargeted | 4 | Embedded Swift has no run-time to speak of and has been shown on the AVR. Generic code and existential types grow quickly; reference counting wants a heap |
| A Pascal- or BASIC-like byte-code interpreter (p-code, Tiny BASIC on the part) | Written in C | 4 | Works, at ten to fifty times slower than compiled code and with a kilobyte or two of interpreter. Only worth it if programs must be changed without a programmer attached |
| An interactive Forth on the part | A kernel in assembly that writes its own flash | 6 | The classic Forth experience, and the PIC16F1 can program itself. The cross-compiler here gives smaller and faster programs; an interactive one gives a prompt on the serial port. A separate project |

## Not suitable

| Language | Why | Fit |
|---|---|---|
| TinyGo | Goroutines, a garbage collector and interfaces need RAM in kilobytes before the program starts | 2 |
| MicroPython, CircuitPython, Lua, JavaScript (Espruino), Ruby (mruby) | Interpreters of 100K bytes and more, with a heap | 1 |
| Lisp and Scheme (uLisp, PICOBIT) | A heap of cells and a collector; the smallest need 2K of RAM and 16K to 32K of flash for the interpreter alone | 2 |
| Java (small JVMs), C#, WebAssembly | A virtual machine larger than the part | 1 |
| Haskell, OCaml, Erlang as themselves | A run-time and a heap by definition (as generators of C, see above) | 0 |
| Fortran, COBOL | Flang's run-time library is megabytes; nothing about the languages helps here | 1 |
| Objective-C | Needs its messaging run-time | 1 |
| Python, Go, Java subsets compiled ahead of time | Each drops what makes the language itself (dynamic types, the collector, objects on a heap) and what remains is a worse C | 2 |

## What decides it

Three things came out of building five of these.

**The route matters more than the language.** Rust and Zig were each a
small job, because their compilers already produce LLVM IR for a machine of the
same shape and the retargeting is a text filter. Any language with an LLVM
front end for the AVR (D, Swift, Ada) could come the same way; any
language that generates plain C needs nothing at all.

**What costs on this processor is hidden work, not syntax.** Templates,
generics, traits, `comptime` and `constexpr` are free: they are gone
before the code generator starts. A formatter, a general sort, a string
class, a panic path or an allocator is thousands of words, in whatever
language it is written. The device libraries here for C++, Rust and Zig
compile to within 30 words of one another because they hide nothing.

**The hardware return stack is the real limit on language style.** Sixteen
levels rule out recursion as a habit and make deep layering expensive in
any language. What makes that manageable is not the language but the
linker's count of levels, which works for all five: it reads the calls of
C, C++, Rust, Zig and Forth alike and says how deep the program goes.
