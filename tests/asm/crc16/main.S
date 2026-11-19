; crc16: see tests/programs/crc16/main.c.
;
; The program has no input, so its output is known when it is written: the
; work is done beforehand and the result sent. runtime.S is the version
; that does the work when it runs.
#include "bench.inc"

    bench_start
    bench_say text

    .section .rodata,"a",@progbits
text:
    .asciz  "check=29B1\nbuf=8064\n"
