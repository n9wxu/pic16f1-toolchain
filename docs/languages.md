# Languages other than C

What each language can do on the PIC16F1, how it is built, what was chosen
and why, and what is left out. C itself is described in
[toolchain.md](toolchain.md).

| Language | How it gets to the part | State |
|---|---|---|
| [C++](#c) | Clang's own C++ front end, the same compiler as C | The language except exceptions and run-time type information; the standard library's headers; tested on the simulator and on a PIC16LF1937 |

All of them end in the same code generator, linker, runtime library and
debugger as C, so what [toolchain.md](toolchain.md) says about optimization
levels, static frames, the hardware stack count, `-mdebugger` and LLDB
holds for them too.

## C++

```sh
pic16f1-cc -mcpu=pic16f1937 -Os -std=c++20 -o blink.hex blink.cpp
```

`pic16f1-cc` compiles `.cpp`, `.cc` and `.cxx` files as C++; C and C++
sources can be mixed in one command, and a function shared between them is
declared `extern "C"` as usual. `#include <xc.h>` works unchanged, with
`#pragma config`, `__delay_ms` and the rest.

### What works

Checked by `tests/cxx/features.cpp` on two devices at five levels, in
both code models, whole-program and file by file, through both instruction
selectors:

- classes, constructors and destructors in the right order, references,
  overloading, operators, default arguments, namespaces, `enum class`;
- virtual functions, abstract classes, multiple inheritance, virtual base
  classes;
- pointers to member functions (virtual and not) and to data members;
- templates of every kind, `constexpr` (a table computed by the compiler
  is placed in flash and costs no RAM), `static_assert`, `auto`, range
  `for`, structured bindings, lambdas with and without captures;
- objects with constructors at file scope, `init_priority`, local statics
  constructed on first use;
- placement `new`; `new` and `delete` with the C library;
- an interrupt handler: `extern "C" void __attribute__((interrupt)) isr()`.

The standard is whatever Clang 23 supports (`-std=c++17`, `c++20`,
`c++23`).

### What is left out, and why

| Left out | Why | Instead |
|---|---|---|
| Exceptions (`throw`, `try`) | Unwinding needs tables and a run-time library larger than the parts this is for, and a second stack walk the hardware return stack cannot give | Return a value: `std::optional`, `std::expected`, an error code |
| Run-time type information (`dynamic_cast`, `typeid`) | It needs the same library, and a type description in flash for every class with a virtual function | A virtual function, or `std::variant` |
| Destructors of global objects | A program here does not end; `exit` stops the processor | `-fc++-static-destructors` registers them with `atexit`, which records nothing |
| A lock around the first use of a local static | There are no threads. An interrupt handler that could be first to use the same static as the main program would need one | Construct such an object before interrupts are enabled |

`pic16f1-cc` passes the matching Clang options (`-fno-exceptions
-fno-rtti -fno-c++-static-destructors -fno-threadsafe-statics
-fno-use-cxa-atexit`) to every C++ source. Any of them can be turned back
on after them on the command line, but for the first two there is nothing
to link against.

### Choices

- **`main` is `int main()`**, and is the function the startup code calls.
  (Clang treats `main` as an ordinary, mangled function in a freestanding
  program; for this target it does not.)
- **Constructors of global objects** run from the startup code after data
  is initialised and before `main`. The compiler lists them in
  `.init_array`, which the linker script puts in flash; the loop that calls
  them (`toolchain/runtime/runctors.c`, eleven words and a table entry per
  file with such objects) is linked only into a program that has some. C
  code gets the same from `__attribute__((constructor))`.
- **A pointer to a member function** keeps its "virtual" flag in the
  adjustment field, not in the low bit of the address (the variant of the
  Itanium C++ ABI that MIPS and ARM use): a function's address here is a
  word address and can be odd.
- **Virtual tables are in flash.** A class with virtual functions costs two
  bytes of RAM per object for the pointer to its table, and each virtual
  call is a call through a pointer.
- **A virtual call is a call through a pointer** to everything the linker
  knows: the count of hardware stack levels takes it to reach every
  function whose address is taken, so it is an overestimate for a program
  with many virtual functions, and such functions keep the documented
  calling convention (no argument or result in W). Where the compiler can
  see the object's type it calls the function directly, and with
  whole-program compilation it usually can.
- **`operator new`** is `malloc` and returns a null pointer when memory
  runs out. `operator delete` is `free`; a class with a virtual destructor
  refers to it even if nothing is ever deleted, so it also links, as
  nothing, without the C library.
- **The C library is linked for a C++ program without `--libc`**, if it is
  installed, because the C++ standard headers are built on the C headers.
  Only what the program uses is linked; `--no-libc` turns that off.
- **`-g` changes no code**, as for C. (Two places in the code generator
  that let debug information change a decision were found by the C++ tests
  and fixed, and functions are now laid out in the order of their names,
  because the order the compiler emits them in can depend on `-g`.)

### The standard library

`scripts/build-cxx.sh` (after `scripts/build-picolibc.sh`) installs the
headers of libc++, configured for a machine without threads, files,
locales, wide characters or exceptions, and builds `libc++.a` with the few
things the headers leave to compiled code.

| Works | |
|---|---|
| Language support and utilities | `<cstdint>` `<cstddef>` `<climits>` `<limits>` `<type_traits>` `<concepts>` `<compare>` `<initializer_list>` `<new>` `<utility>` `<tuple>` `<optional>` `<variant>` `<expected>` `<functional>` `<bit>` `<ratio>` |
| Containers and views without allocation | `<array>` `<span>` `<string_view>` `<bitset>` |
| Algorithms | `<algorithm>` `<numeric>` `<iterator>` `<ranges>` |
| With the heap | `<memory>` (`unique_ptr`, `shared_ptr`) `<vector>` `<string>` `<map>`, `std::function` |
| C headers | `<cstring>` `<cstdlib>` `<cmath>` `<cstdio>` `<cassert>` |

| Does not work | Why |
|---|---|
| `<iostream>` and the other streams, `<locale>`, `<format>`, `<print>`, `<regex>`, `<filesystem>` | Not built: each needs locales or files, and tens of thousands of words |
| `<thread>` `<mutex>` `<atomic>` `<chrono>` | No threads or clock; the last two do not compile for a 16-bit `int` |
| `std::to_chars` and `std::from_chars` for floating point | Need 64-bit `double` arithmetic |

Sizes on the PIC16F1937 (8,192 words of flash, 496 bytes of RAM):

| Program | `-O1` | `-Os` | RAM |
|---|---|---|---|
| `tests/cxx/features.cpp`: the language, no library | 1,231 | 1,058 | 24 |
| `tests/cxx/library.cpp`: `array`, algorithms, `optional`, `tuple`, `span`, `string_view`, `bitset`, `variant` | 1,378 | 1,311 | 7 |
| `tests/cxx/heap.cpp`: `new`, `unique_ptr`, `vector`, `string`, `std::function` | 6,898 | 6,703 | 68 |
| `tests/cxx/hal.cpp`: pins, serial port, a timer interrupt | 409 | 299 | 3 |

Templates and `constexpr` cost nothing by themselves; what costs is what
the library does behind a simple name. Two to know about:

- **`std::sort` is about 8,000 words.** libc++'s is written for large
  machines (it sorts in blocks with 64-bit bit masks). `library.cpp` shows
  an insertion sort in two lines from `std::rotate` and
  `std::upper_bound`, which is 150 words.
- **`std::string` and `std::vector` are 2,000 to 3,000 words each** with
  the allocator, and the heap needs RAM the part hardly has. `std::array`,
  `std::span` and `std::string_view` cost what the equivalent C costs.

### The device library, `<pic/pic.hpp>`

A header-only library for the PIC16F193x (`toolchain/include/pic/pic.hpp`),
in namespace `pic`. Nothing in it takes RAM or code unless it is used.

```cpp
#pragma config FOSC = INTOSC, WDTE = OFF, MCLRE = ON, LVP = ON
#define _XTAL_FREQ 8000000UL            // before the header: delays and baud rates
#include <pic/pic.hpp>

using Led = pic::Pin<'E', 0>;           // RE0, known at compile time

int main()
{
    pic::set_clock(pic::Clock::MHz8);
    Led::digital();
    Led::output();
    pic::Uart::begin(9600);
    for (;;) {
        Led::toggle();                  // one XORWF
        pic::Uart::write("tick\n");
        pic::delay_ms(500);
    }
}
```

| | |
|---|---|
| `pic::Pin<'E', 0>` | `output()` `input()` `digital()` `analog()` `set()` `clear()` `toggle()` `write(bool)` `read()` `driven()`; each a single bit instruction |
| `pic::Port<'D'>` | `port()` `tris()` `lat()` `ansel()`: the registers of a port, by letter, for templates |
| `pic::set_clock(pic::Clock::MHz8)` | The internal oscillator, 500 kHz to 16 MHz |
| `pic::delay_ms(n)` `delay_us(n)` `delay_cycles(n)` | At least the time asked for; with a constant, computed by the compiler |
| `pic::Uart` | `begin(baud)` `write(byte)` `write(text)` `print(number)` `print_hex(byte)` `flush()` `available()` `read()`; the EUSART on RC6 and RC7, 8N1, polled |
| `pic::Adc` | `begin()` `read(channel)` (10 bits) `fixed_reference_on(gain)`; channels `temperature`, `dac`, `fixed_reference` besides the pins |
| `pic::Eeprom` | `read(address)` `write(address, value)` `update(address, value)` (writes only a changed byte) |
| `pic::Timer0` | `begin(shift, interrupts)` `count()` `wrapped()` |
| `pic::enable_interrupts()` `disable_interrupts()`, `pic::CriticalSection` | The last turns interrupts off for the life of the object and restores them |

### Tests

| | |
|---|---|
| `scripts/test-cxx.sh` | 47 builds on the simulator: the four programs above, on two or three devices, at up to five levels; `-g` changes no code |
| `tests/hardware/cxx.cpp`, run by `tools/hwtest.py` on a PIC16LF1937 | Pass: a constructor of a global and a virtual call; `delay_ms(100)` measured as 48 counts of timer 0 where 49 are expected; four EEPROM bytes written and read back; the 1.024 V reference read as 327 of 1023 (a 3.2 V supply) |

`tools/hwtest.py` has the PICkit 2 probe's OpenOCD program a part, runs
the program under LLDB to a function named `hw_done`, and reads the result
the program left in `LATD` and any variables named on its command line.
