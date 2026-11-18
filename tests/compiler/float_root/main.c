/* Newton's method square root: a chain of float operations. */
#include "bench.h"
#define NOINLINE __attribute__((noinline))

static volatile uint8_t one = 1;

static uint32_t bits(float f)
{
    union { float f; uint32_t u; } v;
    v.f = f;
    return v.u;
}

NOINLINE static float root(float x)
{
    float r = x * 0.5f;
    for (uint8_t i = 0; i < 12; i++) r = 0.5f * (r + x / r);
    return r;
}

void main(void)
{
    float two = 2.0f;
    bench_init();
    if (one != 1) two = 0.0f;
    bench_result32("root2", bits(root(two)));
    bench_result32("root144", bits(root(144.0f)));
    bench_result32("root10", bits(root(10.0f)));
    bench_exit(bits(root(144.0f)) == 0x41400000UL ? 0 : 1);
}
