/* Single-precision add, subtract, multiply and compare. The floating-point
 * routines are large, so the float tests are split to fit in 8K words. */
#include "bench.h"
#define NOINLINE __attribute__((noinline))

static volatile uint8_t one = 1;

static uint32_t bits(float f)
{
    union { float f; uint32_t u; } v;
    v.f = f;
    return v.u;
}

NOINLINE static float add(float a, float b) { return a + b; }
NOINLINE static float sub(float a, float b) { return a - b; }
NOINLINE static float mul(float a, float b) { return a * b; }
NOINLINE static uint8_t cmp(float a, float b)
{
    return (uint8_t)((a < b) | ((a <= b) << 1) | ((a > b) << 2) | ((a >= b) << 3) |
                     ((a == b) << 4) | ((a != b) << 5));
}

void main(void)
{
    float a = 3.25f, b = -1.5f, c = 1000000.0f, s = 0.0f, p = 1.0f;
    bench_init();
    if (one != 1) a = 0.0f;
    bench_result32("add", bits(add(a, b)));
    bench_result32("sub", bits(sub(a, c)));
    bench_result32("mul", bits(mul(a, b)));
    bench_result32("small", bits(mul(1.0e-20f, 1.0e-20f)));
    bench_result32("big", bits(mul(c, mul(c, mul(c, mul(c, mul(c, mul(c, c))))))));
    bench_result16("cmp", (uint16_t)(cmp(a, b) | (cmp(b, a) << 8)));
    bench_result16("cmpeq", cmp(a, 3.25f));
    for (uint8_t i = 0; i < 12; i++) {
        s = add(s, p);
        p = mul(p, 0.7f);
    }
    bench_result32("geo", bits(s));
    bench_exit(cmp(a, b) == (4 | 8 | 32) && bits(add(a, b)) == 0x3FE00000UL ? 0 : 1);
}
