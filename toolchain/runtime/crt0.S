; Startup code for the PIC16F1 LLVM toolchain.
;
; Word 0 holds a jump here. The interrupt handler, if the program has one,
; is placed at word 4 by the linker script (section .isr).
;
; Core register file addresses used below, fixed by the instruction set:
;   0x03 STATUS (bit 2 = Z)   0x04/0x05 FSR0L/H   0x06/0x07 FSR1L/H
; 0x70/0x71 are the compiler's R0/R1 byte registers in common RAM.

; Built twice: as crt0.o, and with -DONE_PAGE as crt0-small.o for programs
; that fit in the first page, where PCLATH stays 0 from reset.
#ifdef ONE_PAGE
#define PAGESEL(target)
    .globl  __pic16f1_one_page
#else
#define PAGESEL(target) movlp page(target)
#endif

    .section .reset,"ax",@progbits
    PAGESEL(_start)
    goto    _start

; Startup runs through the .init sections in order; the linker script places
; them at the start of the code:
;   .init0  _start
;   .init4  copy initialised data to RAM      (copydata.S, in libpicrt.a)
;   .init5  set the stack pointer cell         (stack.S, in libpicrt.a)
;   .init6  clear .bss                        (clearbss.S, in libpicrt.a)
;   .init9  set the stack pointer, call main, call exit
; The compiler refers to __do_copy_data from every module that defines
; initialised data and to __do_clear_bss from every module that defines
; zero-initialised data, which pulls the pieces out of the library. A
; program without such data gets neither loop. Assembly sources that define
; data must make the reference themselves:  .globl __do_copy_data
    .section .init0,"ax",@progbits
    .globl  _start
_start:

    .section .init9,"ax",@progbits
    ; There is no stack pointer to set here. A function with a stack frame
    ; takes it from the cell __sp, which stack.S (in libpicrt.a) defines and
    ; sets up in .init5; a program whose functions all have static frames
    ; refers to neither and has no stack.

    ; No bank is selected here: MOVLB is encoded differently on 32- and
    ; 64-bank devices, and this file serves both. Compiled code selects the
    ; bank itself before every banked access.
    PAGESEL(main)
    call    main

    ; main's result is in R0:R1, which is also where exit takes its argument.
    PAGESEL(exit)
    call    exit

    ; The default exit stops the processor. A program may define its own.
    .weak   exit
exit:
.Lstop:
    sleep
    bra     .Lstop

; For the linker's choice between stack and static frames (see "Static
; frames" in docs/toolchain.md): what this file's code calls. An object
; file without this section is taken to call everything it refers to.
    .section .pic16f1.frames,"",@progbits
    .ascii  "F _start -\nC main\nC exit\n"
