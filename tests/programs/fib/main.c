/* Iterative Fibonacci in 16 and 32 bits. fib(24) = 46368, fib(40) = 102334155. */
#include "bench.h"

static uint16_t fib16(uint8_t n)
{
    uint16_t a = 0, b = 1;
    while (n--) {
        uint16_t t = (uint16_t)(a + b);
        a = b;
        b = t;
    }
    return a;
}

static uint32_t fib32(uint8_t n)
{
    uint32_t a = 0, b = 1;
    while (n--) {
        uint32_t t = a + b;
        a = b;
        b = t;
    }
    return a;
}

void main(void)
{
    uint16_t f16;
    uint32_t f32;
    bench_init();
    f16 = fib16(24);
    f32 = fib32(40);
    bench_result16("fib24", f16);
    bench_result32("fib40", f32);
    bench_exit(f16 == 46368u && f32 == 102334155UL ? 0 : 1);
}
