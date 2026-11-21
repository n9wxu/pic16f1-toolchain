# F1 Evaluation Platform demo

Everything on Microchip's F1 Evaluation Platform (DM164130-1) at once, in
C++23, on its PIC16LF1937: 2,359 words of the part's 8,192 and 61 bytes
of its RAM, with the processor asleep between the 32 ticks a second of
the watch crystal.

| On the board | What the program does with it |
|---|---|
| LCD glass (3½ digits and legends) | Shows a clock, the temperature, the potentiometer or the supply voltage |
| Button S1 | Short press: the next of those four. Long press on the clock: set it. Long press on the temperature: Celsius or Fahrenheit |
| Potentiometer | Read eight times a second: a value from 0 to 1023 on the glass and a bar on the LEDs. While the clock is being set, turning it off centre runs the time forward or back, faster the further it is turned; a press ends that |
| LEDs D1 to D4 | How far the potentiometer is turned: none to all four |
| MCP9800 temperature sensor (I2C) | Found at start-up (addresses 0x48 to 0x4F are tried), kept shut down, and asked for one 12-bit conversion each second |
| 32.768 kHz crystal | Timer 1: the time of day, the tick everything runs on, and the LCD's clock, all of which go on while the processor sleeps |
| Data EEPROM | Keeps which display was showing and the unit across power-off |
| Serial pin (RC6, pin 6 of the PICkit Serial header) | A line each second at 9600 baud: `12:00:07  19.6 C  pot 214  3.22 V` |

The supply voltage is measured with nothing external: the converter reads
the part's 1.024 V reference against VDD.

## Build and load

```sh
make                                  # build/f1-eval.hex
make flash OPENOCD=<directory>        # through the PICkit 2 probe's OpenOCD
```

or open the folder in VS Code with the PIC16F1 Toolchain extension: F7
builds, and **Debug on the board (probe)** programs the part and stops at
`main`. The simulator has no timer 1, LCD or I2C, so there the program
prints its first line and sleeps for good.

A build for the debugger (`make DEBUGGER=1`, or the extension's probe
configuration) leaves out the `SLEEP`: the probe loses a part that sleeps
between its questions. Everything else is the same.

## The files

| File | What it is | What of C++ it leans on |
|---|---|---|
| `board.hpp` | The board: which pin is what, as names for types. No code, no RAM | Variadic templates and fold expressions (`Bar<Pins...>`: the LED bar over any pins); concepts; a `consteval` literal, `1000_ms`, that is a count of ticks and does not compile if the time is not a whole number of them; `static_assert` that the LCD pin list gives the same enable bytes as the board's factory firmware |
| `lcd.hpp` | The LCD controller. `Segment<common, pin>` is a type, so lighting one is a single `BSF`; a digit is seven of them | A `consteval` font: `glyph('P')` is a constant, and a character with no glyph is a compile error; the ten digits are a table the compiler makes from the same function |
| `i2c.hpp` | The MSSP as an I2C master. Every wait is bounded | `std::expected` in place of exceptions; `std::span` for buffers; a `Transaction` whose destructor sends the stop condition on every way out; a concept, `i2c::Bus`, that says what a driver needs of a bus |
| `mcp9800.hpp` | The sensor, over any `i2c::Bus` | `Celsius` as a type of its own (sixteenths of a degree) with `<=>` and `21.5_degC` literals; the conversions checked by `static_assert` against the data sheet's examples |
| `rtc.hpp` | Timer 1 on the crystal, and a three-byte time of day | `constexpr` member functions tested in the compiler: midnight roll-over, setting backwards through midnight, the 12-hour display |
| `button.hpp` | Debounce, short and long presses, in one byte | A concept for the pin, so the same class runs in the compiler on a stand-in: the `static_assert`s feed it runs of samples |
| `main.cpp` | The program | `enum class` with `using enum`; `constinit`; `std::to_underlying`; a `<<` formatter for the serial port that compiles to the calls |

None of it uses a virtual function, the heap or the C library. The tests
written as `static_assert` run on every build and take no
space in the part.

What some of it costs, in words of flash at `-Os`: showing a glyph on a
digit, 56 to 75 (seven tested segments); a number on the glass with its
sign, leading blanks and decimal point, 175; an I2C write of any length,
with its time-outs and errors, 67; the interrupt handler, 14.

## What was checked, and how

On a PIC16LF1937 on the board, through the probe and the debugger, by
reading the program's variables and the LCD's data registers:

- the clock counts seconds at the right rate; the glass registers decode
  to `12 00`, with the colon blinking;
- temperature `20.0` (and `67.7` in Fahrenheit), with the sensor found and
  12-bit one-shot conversions arriving;
- potentiometer `214` with one LED lit; supply `3.22` with the battery
  and V legends;
- a short press steps through the four displays and back; setting the
  clock with the potentiometer left of centre ran it back from 12:00 to
  11:28 in a second, with the S1 legend lit.

The presses were made by the debugger (it wrote the button's counter), not
by a finger; the serial text was checked on the simulator, not on the
pin; and nobody has yet looked at the glass or measured the current.
