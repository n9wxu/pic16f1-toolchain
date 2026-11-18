# Simulator interface

Status: draft v1, awaiting approval.

## Program exit

A program ends by executing `SLEEP` with nothing able to wake the device.
Its exit code is the value in `LATD` at that moment, or `LATC` on a device
with no PORTD. No register exists only in the simulator, so the same binary
runs on silicon, where the code appears on the port pins.

The process exit status of `pic16f1-sim run` is that code, 124 if the cycle
limit is reached, and 125 for a fault.

**Open:** the plan (workstream 6) calls for a reserved register carrying
`printf` output and the exit code. The port-and-`SLEEP` convention replaces
it here; confirm or change.

## Output

Bytes written to the EUSART transmit register while the transmitter is
enabled are the program's output. By default transmission takes no time;
`--uart timed` models the configured baud rate.

## Faults

The simulator stops and reports: hardware stack overflow or underflow, an
instruction fetched from a word the image did not program, an undefined
opcode, and the `RESET` instruction.

## Images

Intel HEX in Microchip's layout, or ELF as described in
[object-format.md](object-format.md).

## Result record

`pic16f1-sim run --json` prints one JSON object:

| Field | Meaning |
|---|---|
| `halt` | `sleep`, `max-cycles`, `stack-overflow`, `stack-underflow`, `unprogrammed`, `illegal-opcode`, `reset` |
| `exit_code` | Value on the exit port |
| `cycles`, `instructions` | Instruction cycles (4 oscillator periods each) and instructions executed |
| `program_words` | Words the image programs below the end of flash |
| `max_stack_depth` | Deepest hardware stack use |
| `interrupts` | Interrupts taken |
| `uart`, `uart_overruns` | Output text, and bytes written while the transmitter was busy |

## Timing model

Every instruction takes one cycle; those that change the program counter,
including a taken skip, take two. Interrupt entry is three cycles after the
instruction that raised it.

**Open:** interrupt latency and all peripheral timing are unverified against
silicon. Modelled peripherals: Timer0 (classic control register only),
Timer2, EUSART transmit, port latches. The watchdog is not modelled.
