/* More arguments than there are argument registers, and calls through
 * pointers with stack arguments. */
#include "bench.h"
#define NOINLINE __attribute__((noinline))

static volatile uint8_t one = 1;

NOINLINE static uint32_t twelve(uint8_t a, uint16_t b, uint32_t c, uint8_t d, uint16_t e, uint8_t f,
                                uint32_t g, uint16_t h, uint8_t i, uint8_t j, uint16_t k, uint32_t l)
{
    uint32_t r = a;
    r = r * 3 + b;
    r = r * 3 + c;
    r = r * 3 + d;
    r = r * 3 + e;
    r = r * 3 + f;
    r = r * 3 + g;
    r = r * 3 + h;
    r = r * 3 + i;
    r = r * 3 + j;
    r = r * 3 + k;
    r = r * 3 + l;
    return r;
}

NOINLINE static uint16_t pick(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e, uint16_t f,
                              uint8_t which)
{
    switch (which) {
    case 0: return a;
    case 1: return b;
    case 2: return c;
    case 3: return d;
    case 4: return e;
    default: return f;
    }
}

NOINLINE static uint64_t wide(uint64_t x, uint64_t y, uint8_t z)
{
    return x + y + z;
}

typedef uint32_t (*twelve_fn)(uint8_t, uint16_t, uint32_t, uint8_t, uint16_t, uint8_t,
                              uint32_t, uint16_t, uint8_t, uint8_t, uint16_t, uint32_t);

void main(void)
{
    twelve_fn fp = twelve;
    uint32_t r1, r2;
    uint16_t p = 0;
    uint64_t w;
    bench_init();
    r1 = twelve(one, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    r2 = fp((uint8_t)(one + 1), 300, 70000UL, 4, 500, 6, 7000000UL, 800, 9, 10, 1100, 12000000UL);
    for (uint8_t i = 0; i < 6; i++) {
        p = (uint16_t)(p * 7u + pick(11, 22, 33, 44, 55, 66, i));
    }
    w = wide(0x0123456789ABCDEFULL, 0x1111111111111111ULL, (uint8_t)(one + 1));
    bench_result32("direct", r1);
    bench_result32("pointer", r2);
    bench_result16("pick", p);
    bench_result32("wide_hi", (uint32_t)(w >> 32));
    bench_result32("wide_lo", (uint32_t)w);
    bench_exit(r1 == 398574UL ? 0 : 1);
}
