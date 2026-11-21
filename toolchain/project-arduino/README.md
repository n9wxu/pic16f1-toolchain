# @NAME@

An Arduino sketch for the @DEVICE_UPPER@, built with the PIC16F1 LLVM
toolchain at `@TOOLCHAIN@`.

## In VS Code

Open this folder. The **PIC16F1 Toolchain** extension does the work.

| | |
|---|---|
| Build | F7, or Terminal > Run Build Task. Errors and warnings appear in the Problems panel, at their lines of the `.ino` file |
| Debug on the simulator | F5 with "Debug on the simulator". What the sketch prints with `Serial` appears in the Debug Console. The simulator has the timer `millis` and `delay` run on and the serial port; not the A/D converter, PWM, I2C or SPI |
| Debug on the part | "Debug on the part (probe)" and F5 |

## From a terminal

```sh
make            # build/@NAME@.hex and .elf
make debug      # LLDB on the simulator
make size
```

## The Arduino language here

`pinMode`, `digitalWrite`, `digitalRead`, `analogRead`, `analogWrite`,
`millis`, `micros`, `delay`, `Serial`, `attachInterrupt`, and the `Wire`,
`SPI` and `EEPROM` libraries. A pin is `PIN_RA0` to `PIN_RE3`, or its
number (8 times the port plus the bit); `A0` to `A13` are the analog
inputs, `LED_BUILTIN` the first LED of the F1 Evaluation Platform. The
oscillator is the internal one at `F_CPU` (in the Makefile; 16 MHz).

There is no `String` class and no `tone`; see the toolchain's
`docs/arduino.md` for the whole list and for what things cost.
Other `.ino`, `.cpp`, `.c` and `.h` files in this folder are part of the
sketch; libraries are looked for in a `libraries` folder here, in the
toolchain's, and in `Documents/Arduino/libraries`.
