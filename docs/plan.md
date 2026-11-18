# PIC16F1 Open Toolchain: Project Plan

Version 3. Combines the v2 plan with the technical design decisions and lab setup worked out afterward.

Items marked **(verify)** are facts or tool behaviors that must be confirmed against current datasheets, tool releases, or project policies before work depends on them.

---

## Contents

1. Scope and goals
2. Toolchain architecture
3. Technical design
4. Workstreams
5. Hardware lab
6. Agent operating model
7. Gates
8. Risks
9. Decisions required at G0
10. Appendix A: Target facts the design relies on
11. Appendix B: Glossary

---

## 1. Scope and goals

### 1.1 Scope

- **Devices:** the enhanced mid-range core, meaning PIC16(L)F1xxx and the newer PIC16F1xxxx families (18xxx, 152xx, and others) that share its instruction set.
- **Out of scope:** classic mid-range parts (PIC16F8xx and similar). They lack `MOVIW`, `MOVWI`, `ADDFSR`, and the linear FSR space. They could be added later as a reduced subtarget.
- **Deliverables:**
  - A compiler backend and optimizer (LLVM).
  - Front ends for C, C++, Rust, and Zig.
  - A linker (LLD) and binary utilities.
  - Runtime and libraries.
  - A device database and the generators that feed every other component from it.
  - A simulator.
  - A debugger (LLDB).
  - An RP2040-based ICSP probe with firmware.
  - OpenOCD support for programming and on-chip debugging.

### 1.2 Goals

| Goal | Measure |
|---|---|
| Correctness | Conformance suites pass on the simulator and on silicon, and the bank/page verifier reports nothing across the full test run |
| Code density | Within 15% of XC8 free mode at G3. Within 5% of XC8 PRO at G5 |
| Languages | C and C++ in freestanding mode. Rust `core` with async. Zig freestanding |
| Open hardware and software path | Program and debug with no Microchip tools in the loop. Microchip tools are used only as a reference |

### 1.3 Code organization

- One fork of `llvm-project` (LLVM, Clang, LLD, LLDB). rustc and Zig forks are built against it.
- Generic fixes go upstream. The PIC target itself stays out of tree unless a long-term maintainer group forms.
- All three compilers use the same LLVM fork version per toolchain release, so cross-language LTO works. A version matrix records the combination.

---

## 2. Toolchain architecture

```
      Device database (generated from Microchip device packs)
        │ SFRs, banks, memory sizes, config words, peripherals, programming algorithm, errata
        ├──► C headers / Rust PACs / Zig packages / linker scripts / OpenOCD configs / simulator models
        ▼
 Clang (C/C++) ─┐
 rustc fork ────┼─► LLVM IR ─► LLD + LTO ─► PIC backend + whole-program passes ─► ELF ─► Intel HEX
 Zig fork ──────┘                 ▲                                               │
                                  └──────── profile from simulator ◄─────────────┤
                                                                                  ▼
          Simulator (gdb-remote) ◄── LLDB ──► OpenOCD (icsp transport) ──► RP2040 probe ──► target
                                                                                   ▲
                    PICkit 3 + MPLAB IPE/MDB (reference only) ── Saleae captures ──┘
```

**Design rule:** keep everything PIC-specific inside LLVM, as IR attributes, metadata, and intrinsics. Front ends only spell those constructs. Then every language gets the calling convention, interrupt ABI, static frames, and hardware-use hints with no duplicated logic.

The one exception is argument-passing (ABI) lowering. Clang, rustc, and Zig each do it in their own front end, so it has to be implemented three times and checked against a single specification with cross-language tests.

---

## 3. Technical design

### 3.1 Register and memory model

- **W** is the only real ALU register. It is modeled as a single-register class.
- **All free data memory is allocatable.** After globals and address-taken objects are placed, every remaining GPR byte becomes a physical register "slot." The largest parts have about 4 KB of RAM, so up to about 4,000 i8 slots. AMDGPU shows LLVM can handle register files that size.
- **Slots are abstract during register allocation.** The allocator assigns slot IDs with no bank meaning. With so many slots it almost never spills, so allocation reduces to coloring live ranges.
- **Common RAM** (16 bytes at 0x70–0x7F) is not given out by the allocator. The bank-mapping pass assigns it to the values that save the most `MOVLB` instructions. 2–4 bytes are reserved as fixed scratch for prologues, epilogues, FSR save sequences, libcall glue, and ISR entry.
- **FSR0 and FSR1** are 16-bit pointer registers. Whether FSR1 is available depends on the function's stack model (3.3).
- **STATUS** is modeled as an implicit def of ALU instructions. It has only C, DC, and Z. C is an inverted borrow.

### 3.2 Data layout and type sizes

- Pointers 16 bits, byte alignment everywhere, native integer width 8: approximately `e-p:16:8-i16:8-i32:8-n8`.
- C: `int` 16, `long` 32, `long long` 64, `float` 32, `double` 32 by default (matching AVR and XC8), with 64-bit `double` as an option.
- **Unified pointers.** The FSR space covers RAM (traditional banked addresses at 0x0000–0x0FFF, linear GPR at 0x2000+) and flash (0x8000+). One 16-bit pointer type can reach both, so the 3-byte generic pointers that PIC18 compilers need are unnecessary.
- **Address space 1** is program memory, for constant data when explicit placement is wanted.
- **Narrow-pointer address spaces** (8-bit pointers into one bank or into common RAM) are a later option. LLVM's DataLayout allows a different pointer size per address space, and Zig can name them directly in pointer types.

### 3.3 Stack models

**Default: static frames.** During LTO, a call-graph pass with IPRA overlays the frames of functions that can never be live at the same time.

**The software stack (FSR1-based, `MOVIW`/`MOVWI k[FSR1]`, `ADDFSR`) is used only for:**

1. Recursion that isn't in tail position. LLVM's tail-recursion elimination turns tail recursion into loops first.
2. VLAs and `alloca`.
3. Calls through raw addresses that aren't checked, for example a received integer cast to a function pointer. The conservative call graph has an edge from that call to every function, which forms a cycle, which is treated as recursion.
4. Code that arrives at run time (written to flash, then called).
5. Functions shared between contexts where the cost model picks the software stack over the alternatives (3.4).

**Cases that do not need a software stack:**
- Indirect calls through tables or through address-taken function pointers. A static overlay stays sound if every indirect call is assumed to reach every address-taken function of matching type.
- Data-driven selection among known functions (state machines, handler tables).
- External code that is built as part of the full program, or brought in as fixed constraints.
- A bootloader the application never calls. It contributes only linker-script constraints (3.10).

**Compatibility with XC8.** Accept `-mstack=compiled|hybrid|reentrant` with XC8's meanings, and provide Clang attributes that match XC8's per-function specifiers.

**Hardware stack.** It is 16 levels and holds return addresses only. The call-graph analysis computes the maximum depth, including one level for an interrupt at maximum depth plus the ISR's own call depth. A link-time error reports the full call chain when the limit is exceeded. Section 3.9 describes how to go past 16 levels.

### 3.4 Interrupts and execution contexts

- PIC16F1 has one interrupt vector. Without an RTOS there are exactly two contexts: main and ISR.
- `__attribute__((interrupt))` emits `RETFIE`. The hardware shadows W, STATUS, BSR, FSR0/1, and PCLATH automatically. The ISR saves only the slots it uses, or ISRs get a disjoint slot set.
- **Code generation order:** generate the ISR call tree first, then reserve its slots for every other function.
- **Functions reachable from both contexts.** Choose one option per function and apply it to the function's whole callee subtree:

| Option | Flash | RAM | Main-thread latency | ISR latency |
|---|---|---|---|---|
| Duplicate the function | Function size × 2 | Two frames | None | None |
| Software stack | Small (prologue/epilogue) | Separate stack area | Slightly slower access | Slightly slower access |
| Mask interrupts around calls from main | A few words per call site | One frame | None | Delayed for the length of the call |
| The ISR saves and restores the shared frame | A small copy loop | Frame + save area | None | About 2 cycles per frame byte |
| Busy flag: the ISR saves the frame only when main is inside the function | Flag set/clear plus a test | Frame + save area | Set/clear flag | Usually none |

  The choice depends on the flash budget, the RAM budget, and an ISR latency limit set by the user. Duplicating small leaf functions is usually cheapest. Profile data can estimate how often the ISR interrupts main inside the shared function, which favors the busy-flag option when collisions are rare.

- **More contexts.** A preemptive RTOS, or stackful tasks (3.9), makes every task its own root. That multiplies duplication costs, so stackless designs such as Rust async are preferred.

### 3.5 Instruction selection

GlobalISel, following llvm-mos.

- **Legal types:** i8 only. i16/i32/i64 become carry chains using `ADDWFC` and `SUBWFB`.
- **Multiply, divide, variable shifts:** libcalls or loops. Constant shifts use `LSLF`, `LSRF`, `ASRF`. Multiply uses MATHACC hardware where present and profitable (3.8).
- **Compare and branch:** subtract, skip, branch. Signed compares XOR both operands with 0x80 or test sign bits, since there is no N or V flag.
- **Instructions that write back to the file register.** Patterns for every `op f,f` form, so `x++` on a global is one `INCF x,f`, not a load into a register, an increment, and a store back.
- **Bit operations.** Single-bit masks map to `BSF`, `BCF`, `BTFSC`, `BTFSS`. **A volatile load, single-bit `or`/`and`, and volatile store fold into `BSF`/`BCF`.** That is legal because the instruction still reads and writes the register exactly once. LLVM targets usually refuse to fold volatile operations, so this must be enabled deliberately. Without it, every SFR bit write takes 3–4 instructions where XC8 emits 1.
- **Atomics.** PIC16F1 is single-core with no DMA, so every 8-bit atomic RMW is implemented correctly by clearing GIE briefly. An `atomicrmw or`/`and` on i8 with a constant single-bit mask and an unused result becomes `BSF`/`BCF`, which an interrupt can't split. Targets declare full 8-bit atomics.
- **Jump tables:** `BRW`. **Constant byte tables:** `RETLW` sequences or `MOVIW` through an FSR pointed at flash.
- **Indirect calls:** `CALLW`.

### 3.6 Post-RA and module-level passes

| Pass | Function |
|---|---|
| BSR tracking | Forward dataflow over the known bank. Inserts `MOVLB` only where the bank changes. Calls clobber BSR unless IPRA shows otherwise |
| PCLATH tracking and page selection | Same approach for `MOVLP` before `CALL`/`GOTO` into other 2K-word pages |
| Branch relaxation | `BRA` reaches ±256 words. Longer branches become `MOVLP` + `GOTO` |
| Skip-aware insertion | Nothing may be inserted between a skip instruction and the instruction it skips, including `MOVLB`, `MOVLP`, `ADDFSR`, and FSR setup. Those passes treat the pair as one bundle |
| Skip predication | Skips are modeled as predicating the next instruction so `IfConverter` can use them. `DECFSZ`/`INCFSZ` loop formation |
| Bank/page placement annealer | Section 3.7 |
| FSR access-mode selection | Section 3.7 |
| Stack-depth analysis and return-address spilling | Section 3.9 |
| MachineOutliner hooks | Outlined calls use a hardware stack level, may change BSR and PCLATH, and must not be placed right after a skip |
| `printf` specialization | An LTO pass scans format strings and links a reduced `printf` |
| **Independent verifier** | Proves every direct access has the correct BSR on every path, PCLATH is correct at every call/goto, and no instruction follows a skip illegally. Always runs in assertion builds. Merge gate in CI |

### 3.7 Placement: annealing and FSR windows

**State:**
- Each slot or value is assigned a byte address (a bank or common RAM).
- Each movable global is assigned a bank.
- Each function is assigned a 2K-word page.
- Each multiply site is assigned hardware or software (3.8).

**Cost:** `α · static MOVLB/MOVLP count + β · profile-weighted cycles`. The weights come from `-Os`/`-O2`-style options. Bank capacity is a hard constraint, or a penalty. A bank switch right after a skip instruction costs extra, because the `MOVLB` must be hoisted above the skip.

**Incremental evaluation.** Before annealing, reduce each function to compact per-block access sequences: slot and global IDs, calls with their BSR effect, and CFG edges. A move changes only the functions that touch the moved object, so only those sequences are re-scored.

**Moves:** move one object; swap two objects; move a cluster (a function's slots, or globals accessed together); promote to or demote from common RAM.

**Not annealed.** Arrays and buffers accessed only through FSRs use linear addressing, so their bank doesn't matter and they can span banks. Pack them into leftover space.

**Value-level placement.** After register allocation, the annealer can move individual values to any byte that doesn't conflict with an interfering value, using a whole-program interference graph that includes call-clobber edges. LLVM's greedy allocator only provides the starting point. When RAM isn't tight, this combines register allocation, static-stack overlay, and bank placement into one optimization.

**Schedule.** Geometric cooling with random restarts. For very small programs, an exact ILP solution checks the annealer's quality.

**FSR access modes.** Each access to a direct object gets one of three modes:
- **Direct:** banked addressing with BSR set.
- **Windowed:** `MOVIW`/`MOVWI k[FSRn]`, reaching −32..+31 bytes from the FSR, moves to and from W only.
- **Aimed:** the FSR points exactly at the object, so any file instruction can operate on `INDF`.

Loading an FSR with a constant costs 4 words. `ADDFSR` and `MOVLB` each cost 1 word. A Viterbi-style dynamic program per block over the state (BSR, FSR0, FSR1), with FSR values limited to candidate bases, chooses the modes. The CFG join uses the same dataflow as BSR tracking. The annealer uses a cheaper estimate during search and runs the exact DP on its final result.

**Two address views.** The traditional banked view (0x0000–0x0FFF) includes SFRs and common RAM. The linear view (0x2000+) puts the 80 GPR bytes of each bank back to back. In the linear view, the end of bank *n* is next to the start of bank *n*+1, so one window can cover both banks. The annealer can place hot objects on either side of a bank boundary and reach them through one window. That relaxes the 80-byte bank-capacity limit.

**Where other FSR optimizations belong:**

| Optimization | Location |
|---|---|
| `p->field`, `arr[const]` via `k[FSRn]` | ISel addressing-mode match |
| Software-stack frame access | Frame lowering, `eliminateFrameIndex` |
| `FSRn++` array walks | LoopStrengthReduce via indexed-addressing hooks; GlobalISel indexed load/store combine |
| `memcpy`/`memset`/`memcmp`, multi-byte ops in memory | Custom lowering. Two FSRs in lockstep; `ADDFSR` leaves STATUS unchanged, so the carry survives |
| `INDF` read-modify-write through pointers | ISel |
| FSR windows for direct objects | Placement pass |
| Redundant FSR loads, reload → `ADDFSR` | Late FSR value tracking |
| Flash table reads | ISel for address-space-1 loads |

**FSR constraints:**
- FSR1 is the stack pointer in software-stack functions. It is free in static-frame functions.
- FSR0/FSR1 are included in IPRA clobber masks.
- The ISR may use both freely because of the hardware shadow registers.
- `MOVIW` sets Z, so it must not be scheduled between a Z-setting instruction and the skip that tests Z.

### 3.8 Device peripherals as compute resources

Examples: the PIC16F161x Math Accelerator with PID (16×16 multiply, multiply-accumulate) and CRC/SCAN modules **(verify per datasheet)**.

**Data-driven, not device-specific passes.** `-mcpu` turns on TableGen `SubtargetFeature`s (`FeatureMathAcc`, `FeatureCRCScan`). SFR addresses and banks come from the device database.

| Use | Mechanism |
|---|---|
| i16 multiply | Custom `G_MUL` legalization gated by feature, or a device-specific `__mulhi3` runtime variant |
| Cost of multiply | TTI tables, so strength reduction and unrolling decisions reflect cheap multiply |
| MAC loops | Target IR idiom pass emitting an intrinsic such as `llvm.pic.mac` |
| CRC loops | Recent LLVM recognizes CRC loops (HashRecognize, used by LoopIdiomRecognize) **(verify)**. Map recognized CRCs to the peripheral |
| PID control laws | Builtins (`__builtin_pic_pid_*`) plus a library. Automatic recognition isn't realistic |
| CLC, NCO, and other non-compute peripherals | Headers and libraries only |

**The hardware is not always faster.** Setup (writing operands, reading results, `MOVLB` to the peripheral bank), constant operands (`x*10` = shift and add), operands that fit in 8 bits, context costs, and isolated multiplies (versus chained MACs) all reduce the benefit. The choice is made per site:
- Benefit = (software cost − hardware cost) × block frequency for speed, or the word difference for size.
- If every site uses the hardware, `__mulhi3` can be dropped from flash. That saving is all-or-nothing, so the decision is made over the whole program, inside the annealing state.
- An IR-level pre-filter removes obvious cases (constant operands, narrow known bits).

**Shared-state rules.** The peripheral is shared between main and the ISR. Options: the ISR saves and restores its SFRs (cheap if it does so only when it uses it); reserve it for one context; or mask interrupts. If application code accesses the peripheral's SFRs (visible during LTO as volatile accesses), the compiler stops using it unless the user overrides.

**User controls:**

| Control | Scope | Effect |
|---|---|---|
| `__attribute__((hot))`/`((cold))`, `[[likely]]`, `__builtin_expect` | Existing | Feed block frequencies |
| `#pragma pic mathacc(prefer \| avoid \| off)` | Region or function | Biases the cost model, or disables use |
| `__builtin_pic_mul_hw(a,b)` / `__builtin_pic_mul_sw(a,b)` | Expression | Forces the choice |
| `#pragma pic mathacc_owner(main \| isr)` | Translation unit | Assigns the peripheral to one context |
| `-mmathacc=compiler\|user\|off` | Driver | Who owns the peripheral |

- Precedence: builtins > pragmas > profile > static estimate.
- The region pragma is modeled on `#pragma clang fp`: scoped in Clang, lowered to per-instruction metadata.
- An optimization remark (`-Rpass=pic-mathacc`) reports each site's choice and estimated benefit.
- **For front ends without pragmas:** the backend treats calls to reserved function names such as `__pic_mul_hw` as intrinsics and expands them inline. That works from Rust and Zig with no front-end changes.

### 3.9 Hardware stack access

PIC16F1 exposes the hardware stack through `STKPTR`, `TOSH`, and `TOSL` in bank 31. Access is slow: saving one level takes about 5 instructions, restoring about the same, plus `MOVLB 31`. Microchip's guidance is to keep interrupts masked while `STKPTR`/`TOS` change **(verify)**. A full switch at depth *d* costs roughly 10·*d* + 15 instruction cycles.

**Uses:**
1. **Stackful tasks** (Zig fibers, RTOS threads). The compiler knows the exact depth at each yield site and emits an unrolled copy of exactly that many levels. The bigger cost is RAM: each task becomes its own root in the overlay, needing its own RAM region, 2 bytes per saved level, and handling for functions shared between tasks.
2. **Going past 16 levels.** Selected functions move their own return address into their static frame on entry and push it back on exit, at about 10–12 instructions per call. That frees a hardware level for their callees. The call-graph analysis picks a minimal set on paths deeper than 16, preferring functions that the profile says are rarely called. The depth check counts the freed levels.
3. **Diagnostics.** Panic and fault handlers walk the hardware stack to record a backtrace.

**Constraints:** shadow registers are only saved on interrupt entry, so task switches can't use them. One free level must always remain for an interrupt arriving at maximum depth.

### 3.10 Bootloaders

The application never calls into the bootloader. The bootloader starts the application with a jump and only regains control through a reset (`RESET` instruction or watchdog), so it adds no call-graph edges and doesn't affect frames.

| Mechanism | Content |
|---|---|
| Linker script | Reserved flash range, application reset/interrupt entry offsets, a `NOLOAD` section at a fixed RAM address for mailbox bytes |
| Common header | Mailbox declarations placed in that section, shared values and constants, configuration-word settings |
| crt0 | Sets BSR, PCLATH, FSRs, and peripherals itself. Doesn't zero no-init sections |

Interrupt forwarding through the bootloader costs a `GOTO` (plus `MOVLP` if it crosses pages) and no hardware stack level. In an LLD-based toolchain, LTO runs inside the linker, so the placement passes read the script's regions and pinned sections directly.

### 3.11 Profile feedback

- **Source.** A simulator run, not an instrumented build that might not fit on the part. Program counter values map to `MachineBasicBlock`s through a symbol map emitted by the compiler.
- **Outputs:** block and edge counts; ISR versus main weight; observed pointer targets (to place buffers); maximum observed stack depth (cross-check only).
- **The profile stays valid across annealing.** Placement only adds or removes `MOVLB`/`MOVLP`, so the CFG doesn't change. Branch relaxation changes size, not control flow. Re-simulate only when IR-level code changes.
- **IR-level feedback.** Counts are written as a sample profile keyed by debug line info, in the AutoFDO style, for inlining, block layout, and stack-model decisions in the next build.
- **Rules.**
  - Profiles only influence cost, never correctness. Pointer sizing, ISR slot separation, and stack-depth limits stay sound for paths the training run never executed.
  - Users can weight regions (ISR, control loop) or ignore idle loops.
  - The static term doesn't depend on the profile, so flash size never gets worse because of a poor profile.

### 3.12 Linker and object format

- An ELF `e_machine` value: as far as known, none exists for 8-bit PIC **(verify)**. Use a provisional value and request an official one.
- Program memory is word-addressed, with byte addresses in ELF, as on AVR.
- Relocations: bank number (`(addr >> 7) & 0x1F`), bank offset (`addr & 0x7F`), page bits for `MOVLP`, low 11 bits for `CALL`/`GOTO`, FSR linear addresses, flash addresses (0x8000+).
- Intel HEX via `llvm-objcopy -O ihex`, with byte addresses at twice the word address. Configuration words go at their device-specific addresses.
- Optional later: LLD relaxation that deletes unneeded `MOVLP` instructions, with iterative relaxation as on RISC-V.

### 3.13 Front ends

#### C and C++ (Clang)
- Target definition, `-mcpu`, finding linker scripts, LTO by default.
- Pragmas and builtins from 3.8, XC8-compatible stack controls from 3.3.
- C++: `-fno-exceptions -fno-rtti`, libc++ freestanding, `init_array` handled by crt0, no heap by default. Watch template growth.

#### Rust (rustc fork)

| Area | Work |
|---|---|
| Target | rustc fork against the PIC LLVM fork, plus a target JSON: 16-bit `usize`, `max-atomic-width: 8` with compare-and-swap (atomics implemented by masking GIE) |
| ABI lowering | Implement the PIC C ABI in `rustc_target` to match Clang, checked by cross-language tests |
| Interrupt ABI | `extern "pic-interrupt"`, lowered to the LLVM interrupt attribute |
| `asm!` | Register classes for W and the FSRs |
| Size | `panic = "abort"`, `panic_immediate_abort` via `-Zbuild-std`, `opt-level = "z"`, LTO, `codegen-units = 1` |
| `compiler_builtins` | Normally excluded from LTO. Include it in LTO in the fork, or replace it with the PIC assembly runtime that carries frame and clobber metadata |
| Hints | `core::arch::pic` intrinsics. Region hints need a tool attribute (`#[pic::mathacc(prefer)]`). `#[cold]` and `#[inline]` work today |

Advantages: no integer promotion (8-bit arithmetic stays 8-bit). Constants can live in flash and be reached by ordinary `&'static` references through the unified pointer space; rustc emits immutable statics without interior mutability as LLVM `constant` globals. Generics and closures mostly produce direct calls. `dyn Trait` vtables are constant tables, so static frames stay sound.

Watch: monomorphization growth, hardware stack depth when iterator layers aren't inlined, bounds and overflow check overhead.

**Async Rust:**
- `async fn` compiles to a state machine in MIR. A suspended task needs no stack. Futures live in statics with sizes known at compile time.
- All tasks run in the main context, so async adds concurrency without adding execution contexts. Static frames stay valid. `poll` frames of different tasks share RAM through the overlay.
- RAM ≈ sum of future sizes + deepest `poll` chain + ISR frames.
- When `poll` is inlined into the executor during LTO, `Pin<&mut Self>` becomes a constant address and fields use direct addressing. Otherwise access goes through `k[FSRn]`, and futures larger than 32 bytes need `ADDFSR`.
- Wakers: `RawWakerVTable` is a constant table, so the call graph stays sound. `wake` is shared between main and the ISR and is tiny, so it is duplicated or inlined.
- **Executor design:** one ready bit per task. Per-task wakers generated with const generics make every wake a constant-bit `fetch_or`, which lowers to one `BSF`. The executor tests a bit (`BTFSC`), clears it before polling (`BCF`), and then polls, so a wake during the poll isn't lost.
- **Race-free sleep:** clear GIE; if no task is ready, `SLEEP`; set GIE. A peripheral interrupt with its individual enable set wakes the device even with GIE clear, and the interrupt is handled once GIE is set again.
- Pitfalls:
  - Future layouts can be larger than expected; check them with `-Zprint-type-sizes`.
  - embassy-time uses 64-bit ticks **(verify)**; use a small timer crate with 16- or 32-bit ticks instead.
  - embassy-executor may be too large; a custom executor should take a few hundred words.
  - Naming a future's type for a static needs `type_alias_impl_trait` or macros.
  - Nested `poll`s that aren't inlined each use a hardware stack level.
  - No async recursion, because `Box` isn't available.

#### Zig (Zig fork)

| Area | Work |
|---|---|
| Target | `pic` architecture in `std.Target`, feature tables from TableGen, built against the LLVM fork |
| ABI lowering | Implement in Zig's front end to match Clang |
| Interrupt ABI | `pic_interrupt` in the `CallingConvention` union |
| Async | Zig removed `async`/`await` in 0.11. The `std.Io` design's thread and green-thread backends need a stack per task. Until stackless coroutines exist **(verify status)**, write state machines by hand, or use stackful tasks with the costs in 3.9 |
| compiler_rt | Same LTO visibility issue as Rust; same fixes |
| Size | Minimal root `panic` handler; ReleaseSmall, with `@setRuntimeSafety(true)` where needed |
| Error unions | Error tags are 16 bits by default; shrink the error set width if supported **(verify)** |
| Odd-width integers | Mostly in `packed struct`s; check code generated for odd-width arithmetic |
| Hints | `@branchHint`; reserved intrinsic function names (3.8) |
| Upstream | Zig reportedly does not accept LLM-written contributions **(verify)**. Keep all Zig work in the fork |

Advantages: address spaces in pointer types (`*addrspace(.flash) const u8`, and later narrow-pointer spaces); `comptime` and `packed struct` for register definitions generated from the device database, in the style of microzig.

---

## 4. Workstreams

### Summary

| WS | Workstream | Parallel lanes | Depends on | Oracle | Human involvement |
|---|---|---|---|---|---|
| 1 | Device database and generators | Parser, schema, one lane per output | — | Device packs, datasheets | Pack license review |
| 2 | LLVM backend | MC → C MVP → Opt I → Opt II; passes are separate lanes | WS1, WS9 | Spec, simulator, differential, verifier | Approve ABI, stack model, relocations |
| 3 | LLD port | Relocations, scripts, LTO hooks, HEX | WS2 MC | Readback via PICkit 3, simulator | — |
| 4 | Runtime and libraries | crt0, runtime, picolibc, libc++, Rust crates, Zig package | WS2 C MVP | Conformance suites | — |
| 5 | Front ends | Clang, rustc, Zig | WS2 C MVP, ABI spec | Cross-language ABI tests, language suites | Upstreaming |
| 6 | Simulator | Core, peripherals, gdb-remote, traces | WS1 | Silicon via lab, MPLAB simulator | — |
| 7 | LLDB | Arch/ABI plugin, address spaces, unwinding | WS2 MC, WS6 | Simulator debug sessions | — |
| 8 | Probe, OpenOCD, debug | Firmware, host library, OpenOCD, debug RE, PCB | WS1, lab | PICkit 3 readback, Saleae captures | Legal (debug RE), PCB safety review |
| 9 | Testing and infrastructure | CI, benchmarks, differential, lab API, rebases | — | — | Benchmark definitions |

### WS1: Device database and generators
- **Source:** the `.PIC` files and related data in Microchip's Device Family Packs. Legal review of the pack license before shipping anything derived from them.
- **Schema, per device:**
  - Memory sizes, banks, SFRs and their bit fields.
  - Common-RAM range and linear-memory map.
  - Configuration words.
  - Hardware stack depth.
  - Programming protocol generation (6-bit or 8-bit commands), LVP/HVP support, and voltage limits.
  - On-chip debug availability, or whether the part needs an ICD header.
  - Peripherals with feature flags.
  - Errata.
  - **A source citation for every field.**
- **Outputs:** C/C++ headers; Rust PACs; Zig packages; LLD linker scripts; LLVM subtarget features and the `-mcpu` table; OpenOCD Tcl configs; simulator memory maps; probe voltage limits; the test matrix.
- **Acceptance:** generated headers and linker scripts for all first-wave devices; a sample checked by hand against datasheets.

### WS2: LLVM backend

**Stage A: MC layer**
- Provisional `e_machine`, TableGen registers and instructions.
- Encoder, decoder, assembly parser and printer. The disassembler is reused by LLDB.
- Word-addressed program memory and all relocation types (3.12).
- **Acceptance:** encoding round-trips for every instruction; `llvm-mc` output matches a reference assembler bit for bit.

**Stage B: C MVP**
- Clang `TargetInfo`, GlobalISel i8 legalization, carry chains, libcalls.
- **Software-stack frames only.** It's the simplest correct design.
- Compare/branch lowering, branch relaxation, conservative `MOVLB`/`MOVLP`.
- Interrupt attribute and `RETFIE`. Calling convention written as a specification with tests.
- **Acceptance:** ≥95% of GCC C torture tests at `-O0` and `-Os` on the simulator; picolibc tests pass.

**Stage C: Optimization I**
- LTO module passes: call graph, context analysis, static frames via IPRA, stack-model selection (3.3, 3.4).
- BSR/PCLATH tracking, skip predication, `DECFSZ` loops, bit-op and volatile-RMW folding, file-register write-back patterns.
- 8-bit TTI cost model, stack-depth check, MachineOutliner hooks.
- The independent verifier (3.6).
- **Acceptance:** within 15% of XC8 free on the benchmark set.

**Stage D: Optimization II**
- Slots over all free RAM; annealing of values, globals, and pages; FSR access-mode DP (3.7).
- Profile loop (3.11).
- Peripheral selection with pragmas, builtins, and remarks (3.8).
- Return-address spilling (3.9).
- Shared-function options: frame save in ISR, busy flag (3.4).
- `printf` specialization.
- **Acceptance:** within 5% of XC8 PRO; no verifier findings across the full test run.

**Stage E: research**
- Pointer narrowing via whole-program points-to analysis plus IR rewriting.
- User-defined narrow-pointer address spaces.
- Stackful task support.

### WS3: LLD port
- PIC ELF target, all relocations, memory regions per bank.
- LTO hooks that expose linker-script regions and pinned sections to the module passes.
- HEX output with correct configuration-word placement.
- **Acceptance:** HEX files program correctly via the RP2040 probe and via PICkit 3 (cross-checked readback); bank and region overflows are reported.

### WS4: Runtime and libraries

| Library | Content | Notes |
|---|---|---|
| crt0 | Reset vector, `.data`/`.bss` init, `init_array`, explicit BSR/PCLATH/FSR setup | Doesn't depend on bootloader state. Leaves no-init sections alone |
| Compiler runtime | Integer mul/div, shifts, 32/64-bit helpers, 32-bit IEEE soft float | Hand-written assembly with frame/clobber/depth metadata. MATHACC variants |
| libc/libm | picolibc with tinystdio | Already supports 16-bit `int` (msp430). `printf` specialization hooks |
| C++ | libc++ freestanding | No exceptions, RTTI, or heap |
| Rust | `core`, `compiler_builtins` in LTO, panic handler, PACs, `embedded-hal` implementation, async executor crate | `alloc` optional |
| Zig | microzig-style package: startup, registers, minimal panic | — |

### WS5: Front ends
Section 3.13 has the details.
- **Clang acceptance:** C and C++ conformance, the pragmas and builtins work.
- **Rust acceptance:** `core` builds; the async demo with an interrupt-driven waker runs on silicon.
- **Zig acceptance:** a subset of Zig's behavior tests passes on the simulator.
- **Cross-language ABI suite:** every combination of caller and callee language passes.

### WS6: Simulator
- **Core:** all 49 instructions with exact cycle counts; hardware stack; shadow registers; interrupt and `SLEEP` behavior. Written in Rust as a library so the LTO profile pipeline can call it.
- **Peripherals, in priority order:** interrupts and timers; GPIO; UART; NVM self-write; MATHACC and CRC; others as needed. Driven by the device database.
- **Host channel:** a reserved SFR address carrying `printf` output and the exit code; a gdb-remote stub.
- **Traces:** PC trace, data-access trace, block counts mapped through the compiler's symbol map.
- **Provenance:** no gpsim code (GPL) in the permissively licensed simulator.
- **Acceptance:** matches silicon on the core-validation suite for three device families.

### WS7: LLDB
- Architecture and ABI plugin using LLVM's PIC disassembler.
- Harvard address spaces handled the AVR way: data addresses get a fixed offset in the debugger's address space. The hardware stack is exposed as another address range.
- **DWARF conventions:** static frames have absolute locations. Software-stack frames are FSR1-relative. CFI gives the return address as a hardware stack slot, or as a static-frame slot for functions that spill it.
- Debug against the simulator first, then silicon through OpenOCD.
- **Acceptance:** stepping, breakpoints, variable display, and backtraces for C, Rust, and Zig on the simulator.

### WS8: Probe, OpenOCD, and debug

| Phase | Work | Acceptance |
|---|---|---|
| D0 | Bring up the lab (section 5). Golden programming with PICkit 3 via `ipecmd`. Saleae captures of programming sessions for both command generations | Capture library covers 5+ families |
| D1 | **Probe firmware** in Rust (embassy-rp) with PIO programs for ICSP. **Host ICSP library** in Rust, permissive license, plus a CLI. Implemented from Microchip's published programming specifications: both command generations, LVP entry key, bulk erase, row writes, configuration words, verify, device ID. Validated against captures (5.4) | Probe-programmed images read back identically through PICkit 3 and vice versa, on 5+ families. Timing within spec limits |
| D2 | **OpenOCD** (GPL): `icsp` transport following the `swim`/`stm8` precedent, a probe adapter driver, a `pic16f1` flash driver (program memory, configuration words, EEPROM), generated configs | OpenOCD programs and verifies 5+ families through the probe |
| D3 | gdb-remote debugging of the simulator from LLDB (shared with WS7) | Full debug session on the simulator |
| D4 | **On-chip debug.** Microchip does not publish the PIC debug protocol or the debug executive. Automated capture-and-compare reverse engineering (5.3). **After human legal approval**, implement the protocol in probe firmware and add an OpenOCD `pic16f1` target: halt, resume, step, breakpoints, register and memory access. Also handle devices that need an ICD header. **Fallback:** a software debug monitor linked into the application (UART or ICSP pins), plus simulator debugging | Halt, step, and breakpoints on silicon, or the fallback shipped |
| D5 | **Custom PCB** (RP2040 or RP2350): HVP boost converter, level shifting for 1.8–5.5 V, current-limited target power, USB. Designed in KiCad by agents; reviewed for electrical safety and fabricated by humans. Released as open hardware. Optional probe-rs driver | Passes the D1 and D2 suites; parity with the Pico prototype |

OpenOCD is GPLv2, so the transport, target, and driver code there is GPL. The ICSP library stays permissive or is licensed under both.

### WS9: Testing, benchmarks, and infrastructure
- **LLVM tests:** lit tests for MC, MIR, GlobalISel legalization, and each module pass.
- **Conformance:** GCC C torture tests, picolibc tests, Rust `core` tests, Zig behavior tests, all on the simulator.
- **Differential testing:** Csmith programs compared against the AVR backend on simavr. AVR has the same `int`, `long`, and `double` sizes, so results should match. Also compare behavior against XC8 for the same programs.
- **Benchmarks:** small programs (control loops, protocol stacks, CRC, `printf`-heavy code, ISR-heavy code, XC8 sample projects), measuring flash, RAM, cycles, and ISR latency against XC8 free and PRO. CI catches regressions.
- **Hardware-in-the-loop:** nightly runs on at least one device per family and both programming-protocol generations.
- **Fork maintenance:** rebase onto each LLVM release (about every 6 months), then rebase rustc and Zig; update the version matrix.
- **Provenance:** license and similarity scanning in CI.

---

## 5. Hardware lab

### 5.1 Equipment

| Part | Setup |
|---|---|
| ICSP probe | Raspberry Pi Pico (RP2040). PIO programs for PGC/PGD timing. Level shifting for 1.8–5.5 V targets. MCLR control. Current-limited, switched target power. LVP first; a later revision adds HVP |
| Reference programmer | PICkit 3 driven by MPLAB IPE's command-line tool (`ipecmd`) for golden programming and readback, and by MDB (MPLAB's scriptable command-line debugger) for debug sessions. Pin an MPLAB X version that still supports PICkit 3 **(verify)**. Keep an offline image of the lab host |
| Logic analyzer | **Saleae Logic Pro 16**, driven through the Logic 2 Automation API. Records analog and digital on the same channel at once. A second Pico with sigrok-compatible firmware is an optional backup |
| Targets | Curiosity boards and bare chips in ZIF sockets, one probe each. Per-port USB power control (`uhubctl`). UART back channel through the probe |
| Lab API | Job queue with quotas and per-board write-count tracking |
| Safety interlocks | Probe firmware enforces per-device voltage limits from the device database. Agents can't change them. Only the lab technician can enable HVP |

### 5.2 Saleae channel map

| Channel | Signal | Mode | Purpose |
|---|---|---|---|
| 0 | PGC | Digital | ICSP clock |
| 1 | PGD | Digital | ICSP data, both directions |
| 2 | MCLR/Vpp | Digital + analog | Programming-mode entry; Vpp level and rise time during HVP |
| 3 | Vdd | Analog | Power-up sequencing; droop during erase and writes |
| 4–5 | Target UART TX/RX | Digital | Line up firmware output with ICSP/debug traffic |
| 6 | Target marker GPIO | Digital | Test firmware toggles it at known points |
| 7 | Probe marker GPIO | Digital | The RP2040 toggles it at command boundaries, to line captures up with probe logs |
| 8–15 | Spare | — | A second target, or PICkit 3 and probe on separate boards at once |

- Set the logic threshold for each device's Vdd. LF parts need the 1.2 V or 1.8 V setting.
- The maximum digital sample rate drops as more channels are enabled. ICSP clocks run at a few MHz at most, so 50–100 MS/s is plenty.

**Decoders:**
- **Low-level analyzer** (C++, Saleae Analyzer SDK): frames 6-bit or 8-bit commands, 14/16/24-bit payloads, and PGD turnarounds. The built-in SPI analyzer can't handle variable frame lengths and turnarounds.
- **High-level analyzer** (Python): named commands, addresses, data, and later debug-protocol messages.
- **Offline decoder** (Rust): reads raw transitions exported through the Automation API, so CI and agents don't need the Logic 2 GUI.
- **Independence:** a different agent writes the decoder from the specification and the captures, with no code shared with the probe's encoder. Otherwise one misreading of the spec could end up in both and the tests would still pass.

### 5.3 Automated debug-protocol reverse engineering (D4)

Runs only after human legal approval.

1. Start a Saleae capture through the Automation API.
2. Run a single MDB command against the PICkit 3: `halt`, `step`, set breakpoint, read register.
3. Stop, export, decode.
4. Repeat with one thing changed (register value, address, breakpoint address) and compare the decoded traffic to find which bits carry which fields.
5. Record each confirmed finding in the draft protocol specification with a link to the capture that shows it.

Session setup is studied the same way: debug executive download, entering debug mode, first halt. Marker pins cut long captures down to the region of interest.

### 5.4 Probe validation (D1)

Capture PICkit 3 and RP2040 probe sessions on the same channels and compare timing automatically:
- Setup and hold times.
- Delays between command and data.
- Erase and write waits.
- Vpp ramp shape.

Check both against the programming-specification limits. A pass requires readback to match and timing to stay within limits.

### 5.5 Lab API job types

| Job | Parameters | Returns |
|---|---|---|
| `program` | HEX, device, board class, programmer (probe or PICkit 3) | Status, readback image |
| `run` | Board, duration or exit condition | UART output, exit code |
| `mdb-session` | Script of MDB commands | MDB output |
| `capture` | Channels, sample rates, threshold, trigger/marker, duration | Raw data, decoded frames, measured timings |

`capture` can be combined with any other job type, so a single request covers both the action and the recording of it. The Saleae is a single shared instrument, assigned to one job at a time.

### 5.6 Flash wear

Program-memory endurance is datasheet-specified, typically around 10,000 erase/write cycles **(verify per device)**. The lab tracks writes per board, tests run on the simulator by default, hardware runs are batched, and boards are rotated.

---

## 6. Agent operating model

### 6.1 Rules
1. **Specification before code.** Each subsystem has a specification in the repository: ISA semantics, calling convention, ELF relocations, ICSP protocol per command generation, stack-model rules, DWARF conventions, lab API. Changes require human approval.
2. **Tests come from a different source than the code.** Test authors are separate agents from implementers. Tests are derived from the specification, datasheets, and hardware captures, never from the implementation.
3. **Device facts come only from the device database**, and every field cites its source. No agent types an SFR address, timing, or size from memory.
4. **Small, checkable tasks** with a specification reference, allowed files, and acceptance tests.
5. **Automated gates:** the verifier, differential tests, conformance suites, and size benchmarks block merges.
6. **Provenance:** no copying from GPL sources into permissive components. CI scans for it. Simulator agents never get gpsim source in context.

### 6.2 Roles

| Role | Done by | Responsibility |
|---|---|---|
| Planner | Agent | Splits work into tasks, tracks dependencies, keeps the backlog |
| Implementer | Agents per lane | Code against the specification |
| Test author | Agents, separate from implementers | Conformance, differential, regression tests |
| Reviewer | Agent, never the task's own implementer | Spec conformance, provenance, tests |
| Integrator | Agent | Fork rebases, version matrix, CI repair |
| Lab operator | Agent, restricted permissions | Submits and collects lab jobs |
| Benchmark analyst | Agent | Size and cycle regressions against XC8; opens tasks |
| Architect | **Human** | Approves specifications and gate decisions; reviews a sample of agent work |
| Legal | **Human** | Device-pack licenses, debug reverse engineering, GPL boundaries |
| Lab technician | **Human** | Wiring, board swaps, part replacement, electrical safety, enabling HVP |
| Upstream liaison | **Human** | Upstream contributions under each project's AI policy. LLVM requires a human to be accountable for AI-assisted changes **(verify current policy)**. Zig changes stay in the fork |

### 6.3 Task template
```
id, lane, spec reference, depends-on
allowed paths
acceptance: [test IDs | benchmark threshold | lab job definition]
oracle: spec | datasheet section | hardware capture | differential reference
done: CI green + reviewer approval (+ human approval if a spec changes)
```

### 6.4 Throughput limits
- **Critical path:** MC layer → C MVP → static frames with IPRA → annealer and profile loop. Adding agents doesn't shorten it.
- **Human review:** humans approve specifications and gates and review samples. Reviewer agents check every diff.
- **Lab throughput and flash wear** (5.6).
- **Compute:** shared build cache, sharded tests, incremental builds per lane.

---

## 7. Gates

| Gate | Exit criteria | Lanes that can start once it's passed |
|---|---|---|
| **G0: Foundations** | v1 specifications approved. Device database covers first families. Simulator core passes ISA-semantics tests. Lab runs with the PICkit 3 golden path and the Saleae. Probe reads device IDs | All of WS1, WS6 peripherals, WS8 D1, WS2 MC layer |
| **G1: MC layer and programming** | `llvm-mc` round-trips every instruction. Probe programs and verifies. A HEX file assembled by the toolchain runs on silicon | WS2 C MVP, WS3, WS8 D2 |
| **G2: C MVP** | ≥95% C torture. picolibc passes. Blink on 3 families. Simulator matches silicon on core validation | WS2 Opt I, C++, Rust, WS7 |
| **G3: Optimization I** | Within 15% of XC8 free. OpenOCD programs through the probe | WS2 Opt II, WS8 D4 implementation |
| **G4: Languages and debugging** | Rust `core` builds; async demo on silicon. C++ freestanding works. LLDB debugs on the simulator | Zig, WS8 D5 |
| **G5: Optimization II** | Within 5% of XC8 PRO. No verifier findings. Zig behavior subset passes. On-chip debug works, or the fallback ships | Hardening, research items |
| **G6: Release 1.0** | Documentation, version matrix, installers for the forked toolchains and OpenOCD | — |

The on-chip debug reverse engineering is the most uncertain item. It is off the critical path because simulator debugging and the fallback monitor cover the need until it works.

---

## 8. Risks

| Risk | Mitigation |
|---|---|
| Code that looks right but is wrong; drift from the specification | Separate test authors, the verifier, differential testing against AVR/simavr and XC8, silicon runs |
| Made-up device facts | Device facts only from the database, every field cited |
| License contamination | Provenance scanning, context restrictions, GPL code only in OpenOCD and sigrok/Saleae decoders that are kept separate |
| Upstream AI contribution policies | Work stays in forks. A human liaison decides what goes upstream. Zig stays in the fork |
| Legal questions on debug reverse engineering | No implementation before legal approval. Simulator and fallback monitor cover debugging |
| Hardware damage, flash wear | Firmware voltage interlocks, write tracking, simulator first, HVP gated by a human |
| Human review bottleneck | Humans approve specs and gates and sample diffs. Reviewer agents check everything |
| Losing access to a PICkit 3-compatible MPLAB X | Pinned version, offline lab image. After D0 the capture library holds the reference data |
| Annealer or bank-tracking miscompiles | Verifier as a merge gate, plus differential and silicon tests |
| Not reaching XC8 parity | Go/no-go at G3 and G5. Pointer narrowing is the remaining lever |
| Three compilers on one LLVM fork | Integrator agent, automated rebase CI, one LLVM version per release |
| Rust or Zig binaries don't fit small parts | Target parts with ≥8K words flash and ≥1 KB RAM for Rust and Zig first |
| Device variation (errata, programming-spec differences) | Errata and protocol fields in the device database; per-family silicon tests |

---

## 9. Decisions required at G0

1. Licensing for the device database, simulator, ICSP library, and probe firmware.
2. ELF `e_machine`: request an official value, and choose a provisional one.
3. First device families and the lab board list.
4. Benchmark programs and the measurement method against XC8.
5. The agent platform: sandboxing, permissions per role, CI compute budget, lab API quotas.
6. Who fills the human roles, and the time each needs.
7. The pinned MPLAB X version for PICkit 3, `ipecmd`, and MDB.

---

## Appendix A: Target facts the design relies on

Confirm each against the datasheet and programming specification of every supported device. The device database is the authority.

| Fact | Used by |
|---|---|
| One working register W; ALU ops take a file operand and write to W or to the file | Register model, ISel |
| 32 banks of 128 bytes selected by BSR (`MOVLB`); 80 GPR bytes per bank at 0x20–0x6F | BSR tracking, annealer |
| 16 bytes of common RAM at 0x70–0x7F, visible from every bank | Scratch, annealer |
| Core registers (INDF0/1, PCL, STATUS, FSRs, BSR, WREG, PCLATH, INTCON) mirrored in every bank | Prologues, ISR, verifier |
| FSR0/FSR1 16-bit; traditional view 0x0000–0x0FFF, linear GPR 0x2000+, flash 0x8000+ (low 8 bits of each word) | Unified pointers, FSR windows, flash tables |
| `MOVIW`/`MOVWI` with pre/post increment/decrement and offset k = −32..31; `ADDFSR` k = −32..31 | Software stack, FSR windows, array walks |
| `ADDFSR` doesn't change STATUS; `MOVIW` sets Z | Carry chains, scheduling |
| 16-level hardware return stack, accessible via `STKPTR`, `TOSH`, `TOSL` in bank 31 | Depth analysis, return-address spilling, stackful tasks, backtraces |
| Automatic interrupt context save of W, STATUS, BSR, FSR0/1, PCLATH into shadow registers | ISR design |
| One interrupt vector at 0x0004 | Context analysis |
| `CALL`/`GOTO` 11-bit within 2K-word pages; `MOVLP` sets PCLATH; `BRA` ±256 words; `BRW`, `CALLW` | Page tracking, relaxation, jump tables, indirect calls |
| Skip instructions: `BTFSC`, `BTFSS`, `DECFSZ`, `INCFSZ` | Predication, insertion rules |
| STATUS has C, DC, Z; C is an inverted borrow; no N or V | Compare lowering |
| No hardware multiply in the core; some families have MATHACC | Libcalls, peripheral selection |
| `BSF`/`BCF` are uninterruptible read-modify-write | Atomics, wakers, SFR bit writes |
| A peripheral interrupt with its individual enable set wakes from `SLEEP` even with GIE clear | Race-free executor sleep |
| Two ICSP command generations: 6-bit commands with 14/16-bit payloads, and 8-bit commands with 24-bit payloads | Probe firmware, decoders |
| 14-bit instruction words; program memory word-addressed | MC layer, linker, HEX |

---

## Appendix B: Glossary

| Term | Meaning |
|---|---|
| BSR | Bank select register; chooses which 128-byte bank direct addresses refer to |
| Common RAM | 16 bytes visible from every bank |
| Compiled stack / static frames | Locals at fixed addresses, shared between functions that are never live at the same time |
| FSR | File select register; 16-bit pointer used for indirect access through INDF |
| FSR window | Reaching several objects through `k[FSRn]` offsets from one FSR value |
| HVP / LVP | High-voltage / low-voltage programming entry |
| ICSP | In-circuit serial programming over PGC/PGD |
| IPRA | LLVM's interprocedural register allocation, which records callee clobbers |
| Linear memory | FSR view at 0x2000+ where GPR bytes of all banks are contiguous |
| MATHACC | Math accelerator peripheral (multiply, MAC, PID) on some families |
| MDB | MPLAB's command-line debugger |
| PCLATH | Upper program-counter bits used by `CALL`, `GOTO`, `CALLW`, `BRW` |
| Slot | An allocatable RAM byte treated as a register by the allocator |
| Software stack | Frames allocated relative to FSR1 at run time |
| TTI | LLVM's target transform info, the cost model used by IR optimizations |
