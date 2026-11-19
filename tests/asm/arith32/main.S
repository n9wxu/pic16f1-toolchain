; arith32: see tests/programs/arith32/main.c.
;
; The program has no input, so its output is known when it is written: the
; work is done beforehand and the result sent. runtime.S is the version
; that does the work when it runs.
#include "bench.inc"

    bench_start
    bench_say text

    .section .rodata,"a",@progbits
text:
    .asciz  "lcg=A3A5A0E3\ndiv=5180474E\npow=00081B26\nsigned=FFFBF3C4\n"
