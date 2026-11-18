#include "bench.h"

void main(void)
{
    bench_init();
    bench_puts("hello, pic16f1\n");
    bench_exit(0);
}
