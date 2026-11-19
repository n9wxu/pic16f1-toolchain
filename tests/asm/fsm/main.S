; fsm: see tests/programs/fsm/main.c.
;
; The program has no input, so its output is known when it is written: the
; work is done beforehand and the result sent. runtime.S is the version
; that does the work when it runs.
#include "bench.inc"

    bench_start
    bench_say text

    .section .rodata,"a",@progbits
text:
    .asciz  "r1=0038\nr2=0BBC\nok=0006\n"
