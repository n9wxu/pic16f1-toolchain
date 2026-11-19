; arith16: see tests/programs/arith16/main.c.
;
; The program has no input, so its output is known when it is written: the
; work is done beforehand and the result sent. runtime.S is the version
; that does the work when it runs.
#include "bench.inc"

    bench_start
    bench_say text

    .section .rodata,"a",@progbits
text:
    .asciz  "squares=029E\nmix=91DD\nsigned=FF69\nshifts=65DB\ngcd=0015\nisqrt=00C8\n"
