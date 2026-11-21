# Arduino sketches

The Arduino language for the PIC16F1937 (and the other 40- and 44-pin
PIC16F193x): `setup` and `loop`, the pin, time and serial functions, and
the `Wire`, `SPI` and `EEPROM` libraries, in `toolchain/arduino`. A sketch
is built by `pic16f1-build` (and so by `make` in a project and by the VS
Code extension) the way the Arduino IDE builds one.

```sh
pic16f1-new-project --language arduino Blink     # or "PIC16F1: New Project..." in VS Code
cd Blink && make                                  # build/Blink.hex
make debug                                        # on the simulator
```

```cpp
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);   // one BSF
  delay(500);
  digitalWrite(LED_BUILTIN, LOW);
  delay(500);
}
```

That is 229 words of the part's 8,192 and 5 bytes of RAM.

## In the Arduino IDE

```sh
pic16f1-arduino-ide --openocd <the PICkit 2 probe's OpenOCD directory>
```

copies the platform into the sketchbook (`Documents/Arduino/hardware/pic16f1`)
and notes where the toolchain is. After a restart the IDE has **Tools >
Board > PIC16F1 (LLVM toolchain) > PIC16F1937 (F1 Evaluation Platform)**,
with the oscillator and the optimization level in the Tools menu;
`arduino-cli` knows the board as `pic16f1:pic16f1:f1eval`. Verify compiles
with this toolchain and gives the same program as `pic16f1-build`.

To load a sketch, choose **Tools > Programmer > PICkit 2 probe (OpenOCD)**
and **Sketch > Upload Using Programmer** (`arduino-cli upload -P pk2icsp`).
The plain Upload button wants a serial port, which a part without a
bootloader does not have. The size the IDE reports as bytes is words.
`pic16f1-arduino-ide --remove` takes the platform out again; run the
command again after the toolchain is updated or moved.

## Pins

A pin is a number, 8 times its port plus its bit: `PIN_RA0` (0) to
`PIN_RE3` (35). The registers of the ports stand in that order in the part,
so no table is needed to get from a pin to its register, and **when the pin
is a constant, `pinMode`, `digitalWrite` and `digitalRead` compile to the
bit instructions they stand for**. A pin in a variable goes through a
function, as on any Arduino.

| | |
|---|---|
| `A0` to `A13` | The pins of the analog channels AN0 to AN13 (`A0` is RA0, `A8` is RB2, and so on) |
| `A_FVR`, `A_TEMPERATURE`, `A_DAC` | For `analogRead`: the 1.024 V reference, the temperature indicator, the D/A converter |
| PWM (`analogWrite`) | RC2, RC1, RE0, RD1 and RE2: the five CCP modules on timer 2, about 1 kHz at 16 MHz. 0 is low and 255 is high |
| `INPUT_PULLUP` | Port B only; the other ports have no pull-ups |
| `attachInterrupt` | Every pin of port B, `RISING`, `FALLING` or `CHANGE` |
| `Serial` | TX on RC6, RX on RC7 |
| `Wire` | SCL on RC3, SDA on RC4 |
| `SPI` | SCK on RC3, MISO on RC4, MOSI on RC5 |
| `LED_BUILTIN`, `LED_D1` to `LED_D4`, `BUTTON_S1`, `POT` | The F1 Evaluation Platform's LEDs, button and potentiometer |

Out of reset every pin is a digital input, as a sketch expects;
`analogRead` makes its own pin analog.

## What is here

| | |
|---|---|
| Digital | `pinMode` `digitalWrite` `digitalRead`, and `digitalToggle` (one instruction) |
| Analog | `analogRead` (10 bits) `analogReference` (`DEFAULT`, `INTERNAL1V024`, `INTERNAL2V048`, `INTERNAL4V096`, `EXTERNAL`) `analogWrite` |
| Time | `millis` `micros` `delay` `delayMicroseconds` |
| Serial | `Serial.begin` `print` `println` (numbers in any base, `float` with digits) `write` `available` `read` `peek` `flush` `readBytes` `readBytesUntil` `parseInt` `setTimeout`. Received bytes are taken by the interrupt handler into a buffer of 16 |
| Interrupts | `attachInterrupt` `detachInterrupt` `digitalPinToInterrupt` `interrupts` `noInterrupts` |
| Bits and numbers | `bit` `bitRead` `bitSet` `bitClear` `bitWrite` `lowByte` `highByte` `word` `min` `max` `abs` `constrain` `map` `sq` `random` `randomSeed`, `isDigit` and its kind |
| Other | `shiftOut` `shiftIn` `pulseIn`; `F()`, `PROGMEM` and `pgm_read_byte` (constant data is in flash here without them, and they do nothing) |
| `Wire` | A master: `begin` `setClock` `beginTransmission` `write` `endTransmission` (0, 2, 3, or 5 for a bus that does not respond) `requestFrom` `available` `read`. No wait is without end |
| `SPI` | A master: `begin` `beginTransaction(SPISettings(...))` `transfer` `transfer16` `setDataMode` `setBitOrder` `setClockDivider` |
| `EEPROM` | `read` `write` `update` `get` `put` `EEPROM[i]` `length`. `put` and `EEPROM[i] =` write only bytes that change |

A sketch's `.ino` files are joined, `#include <Arduino.h>` goes first, and
every function is declared ahead of the first, so their order in the
sketch does not matter. Other `.cpp`, `.c` and `.h` files in the folder
are part of the sketch. A library a sketch includes is looked for in a
`libraries` folder of the sketch, then the toolchain's, then
`Documents/Arduino/libraries`; whether one written for another processor
compiles and fits is another matter.

## What is not, and what differs

| | |
|---|---|
| `String` | Not here: it needs a heap, and the part has 512 bytes of RAM. Use `char` arrays and `Serial.print` |
| `tone`, `noTone` | Not written yet |
| `Wire` as a slave, `SoftwareSerial`, `Servo` | Not here |
| `Wire` and `SPI` together | One or the other: the part has one MSSP module for both |
| A pin's number | Not 0 to 13 and A0 to A5 in the order of an Uno's headers: this is not an Uno. Use the `PIN_` names |
| `analogRead(3)` | Takes a pin, so this is RA3; channel numbers as such are not accepted |
| `digitalWrite` on a pin with PWM | Does not turn the PWM off; `analogWrite(pin, 0)` and `analogWrite(pin, 255)` are low and high |
| `delay` | Counted on timer 0, so interrupts do not lengthen it. `delayMicroseconds` is counted in instructions, and the timer's interrupt lengthens it by about 7% |
| `millis` and `micros` at slower clocks | The timer's interrupt takes some 280 instruction cycles, so it comes every 4,096 cycles at 16 MHz and below: every 1.024 ms at 16 and 32 MHz (as on an AVR), every 4 ms at 4 MHz, every 16 ms at 1 MHz. `millis` steps by that much; `micros` by 4 us at 16 and 32 MHz, 16 us at 4 MHz |
| Arithmetic | `MATH = fast` in the project's Makefile (or `--math fast`) links the fast arithmetic library: multiplication, 32-bit division and float multiplication and division one and a half to three times as fast, for more flash |
| The oscillator | The internal one, at `F_CPU`: 16 MHz unless the project's Makefile or `--f-cpu` says 32, 8, 4, 2 or 1 MHz or 500 kHz |
| An interrupt handler of your own | The core has the part's one handler (the timer, `Serial`, `attachInterrupt`). A sketch that needs another is better written without the core |

`analogRead` restarts a conversion the part loses (PIC16F193x errata
DS80000479) and so cannot hang. `EEPROM` writes with the unlock values
armed in a variable and handed to the runtime library's `__eecon_unlock`,
as `<pic/pic.hpp>` does.

## What things cost

Words of flash at `-Os`, 16 MHz:

| Sketch | Words | RAM |
|---|---|---|
| Blink | 229 | 5 |
| `tests/arduino/Basics`: `Serial` with integers, longs and a float, `millis`, `micros`, `map`, `random` | 3,677 | |
| `tests/arduino/Board`: `Wire`, `EEPROM`, `analogRead`, `analogWrite`, `attachInterrupt`, `Serial` with a float | 4,970 | |

Of Blink's 229, 103 are the interrupt handler that keeps `millis` (it
saves the compiler's registers and adds on 32 bits) and 73 are `delay`.
What costs after that is arithmetic: printing a `long` brings 32-bit
division, a `float` the floating-point routines (some 1,500 words), `map`
32-bit multiplication and division. The core is linked as a library, so a
sketch has only the parts it uses: the serial port's receive buffer and
interrupt code only with `Serial`, the pin interrupt table only with
`attachInterrupt`.

## Against an Arduino Nano

The same sketches (`tools/arduino-bench/sketches`: the IDE's first
examples, one for each library, and a benchmark) built for an Arduino Nano
(ATmega328P; `arduino-cli`, Arduino AVR Boards 1.8.6, which is avr-gcc 7.3
at `-Os` as a whole program) and for the PIC16F1937 (`-Os`, 16 MHz).
`tools/arduino_bench.py` makes both tables.

| Sketch | Nano: bytes of flash | of 30,720 | PIC16F1937: words of flash | of 8,192 | in bytes (14 bits a word) | Nano: RAM | PIC: RAM |
|---|---|---|---|---|---|---|---|
| BareMinimum | 444 | 1.4% | 143 | 1.7% | 250 | 9 | 5 |
| Blink | 924 | 3.0% | 229 | 2.8% | 400 | 9 | 5 |
| BlinkWithoutDelay | 848 | 2.8% | 240 | 2.9% | 420 | 15 | 11 |
| DigitalReadSerial | 1,942 | 6.3% | 635 | 7.8% | 1,111 | 188 | 52 |
| AnalogReadSerial | 1,848 | 6.0% | 835 | 10.2% | 1,461 | 188 | 58 |
| ReadAnalogVoltage | 2,988 | 9.7% | 3,211 | 39.2% | 5,619 | 200 | 85 |
| Fade | 1,124 | 3.7% | 298 | 3.6% | 521 | 13 | 9 |
| ButtonInterrupt | 1,032 | 3.4% | 335 | 4.1% | 586 | 14 | 28 |
| WireRead | 4,164 | 13.6% | 1,296 | 15.8% | 2,268 | 404 | 103 |
| EepromCount | 1,710 | 5.6% | 723 | 8.8% | 1,265 | 188 | 54 |
| SpiTransfer | 1,012 | 3.3% | 243 | 3.0% | 425 | 11 | 6 |
| Bench | 4,568 | 14.9% | 4,400 | 53.7% | 7,700 | 547 | 343 |

A word of this part is 14 bits, so the fifth column is what the same code
would be in bytes. Counted that way the PIC16F1 program is smaller than
the AVR's in every sketch without floating point, by a fifth to more
than a half; as a share of the part it is about the same, because
the part is smaller. Floating point reverses it (ReadAnalogVoltage, Bench):
the AVR has a multiplier and a hand-written floating-point library, and
this toolchain's is compiled C. The Nano's RAM figure is its globals (its
stack is extra); the PIC's includes the fixed frames of its functions
and leaves out the 32 bytes kept for the interrupt handler's.

| Piece of Bench | Nano, 16 MHz: ms | PIC16F1937, 16 MHz: ms | times the Nano | with `--math fast`: ms | times the Nano | PIC16F1937, 32 MHz: ms | times the Nano |
|---|---|---|---|---|---|---|---|
| 1,000 times `digitalWrite` high and low, the pin a constant | 9.31 | 4.52 | 0.5 | 4.52 | 0.5 | 2.22 | 0.2 |
| the same, the pin in a variable | 9.60 | 33.78 | 3.5 | 33.78 | 3.5 | 16.52 | 1.7 |
| 1,000 `digitalRead` | 3.00 | 6.10 | 2.0 | 6.11 | 2.0 | 2.98 | 1.0 |
| 1,000 calls of `millis` | 1.03 | 8.74 | 8.5 | 8.73 | 8.5 | 4.28 | 4.1 |
| CRC-8 of 1,024 bytes, bit by bit | 4.34 | 30.05 | 6.9 | 30.05 | 6.9 | 14.69 | 3.4 |
| 1,000 16-bit multiplications, 200 divisions | 3.94 | 17.10 | 4.3 | 17.06 | 4.3 | 8.35 | 2.1 |
| 1,000 32-bit multiplications, 200 divisions | 13.86 | 241.73 | 17.4 | 154.89 | 11.2 | 118.23 | 8.5 |
| 500 terms of a series for pi | 20.94 | 509.14 | 24.3 | 307.16 | 14.7 | 249.00 | 11.9 |
| the primes below 256, ten times | 6.96 | 68.68 | 9.9 | 68.73 | 9.9 | 33.60 | 4.8 |
| 64 16-bit numbers, insertion sort | 1.68 | 8.74 | 5.2 | 8.70 | 5.2 | 4.26 | 2.5 |

Both run on simulators, not on the parts: `pic16f1-sim`, and simavr's
ATmega328P (`tools/arduino-bench/build-avrrun.sh`). Each piece is timed by
the sketch's own `micros`, and what each computes is the same on both.
At 16 MHz the PIC16F1 executes 4 million instructions a second and the AVR
up to 16 million, and the AVR's instructions do more, so a factor of four
to seven is the parts themselves. Where the PIC16F1 is ahead is the pin
that is a constant: one instruction here, a call and a table lookup on the
AVR. Where it is furthest behind is 32-bit multiplication and floating
point, which the AVR does with its multiplier; `--math fast` (`MATH = fast`
in the Makefile, Tools > Arithmetic in the Arduino IDE) links the fast
arithmetic library, which takes those two rows from 17 and 24 times the
Nano's to 11 and 15, for 962 words more in this sketch. See "Faster
arithmetic" in [toolchain.md](toolchain.md).

## Tests

| | |
|---|---|
| `scripts/test-arduino.sh` | Blink at five oscillator settings; `Basics` at two levels and three clocks, checked by the sketch and by what it prints on the simulator; `Board` builds, also for the debugger; a new project builds with `make` and prints `millis` |
| `tests/arduino/Board`, run by `tools/hwtest.py` on a PIC16LF1937 on the F1 Evaluation Platform | Pass: the 1.024 V reference read against VDD; the MCP9800 found with `Wire`, read with a repeated start, and an address with no device reported as such; EEPROM bytes and a `long` written and read back; PWM registers after `analogWrite`; two rising edges counted by `attachInterrupt`; `delay(500)` as 499 to 501 of `millis` |

With the `arduino-cli` of Arduino IDE 2 (0.35.3): the three test sketches
compile to the same sizes as with `pic16f1-build`, `Basics` so built
passes on the simulator, the libraries a sketch includes are found, and
`upload -P pk2icsp` programmed the part. The IDE's own window was not
used.

Not tested on the part: `SPI` (nothing on the board to talk to), `Serial`
on its pins (its text is checked on the simulator), `pulseIn`, `shiftOut`
and `shiftIn`, and `analogWrite` as a waveform (the registers are checked,
not the pin).
