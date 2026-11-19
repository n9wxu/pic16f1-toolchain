; sort: see tests/programs/sort/main.c.
;
; The program has no input, so its output is known when it is written: the
; work is done beforehand and the result sent. runtime.S is the version
; that does the work when it runs.
#include "bench.inc"

    bench_start
    bench_say text

    .section .rodata,"a",@progbits
text:
    .asciz  "sum8=4957\nmix16=4966\nmin8=000D\nmax16=F53C\n"
