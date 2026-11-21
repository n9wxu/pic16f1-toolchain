# Changes

## 0.3.0 (not yet released)

- Arduino sketches: **New Project...** offers Arduino, and a folder with a
  `.ino` file builds and debugs like any other project.
- The A/D converter of the device libraries no longer waits for ever for a
  conversion the PIC16F193x loses (errata DS80000479).
- EEPROM writes arm their unlock values in a variable and hand them to a
  routine of the runtime library; they are not loaded as constants where
  the write is unlocked.

## 0.2.0

The first release of the extension: the toolchain for the system inside
it; projects in C, C++, Rust and Zig; building; debugging on the simulator
and, through OpenOCD and a probe, on the part.
