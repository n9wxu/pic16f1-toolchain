; hello: see tests/programs/hello/main.c.
#include "bench.inc"

    bench_start
    bench_say text

    .section .rodata,"a",@progbits
text:
    .asciz  "hello, pic16f1\n"
