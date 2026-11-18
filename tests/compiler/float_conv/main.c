/* Single-precision divide and conversions to and from integers. */
#include "bench.h"
#define NOINLINE __attribute__((noinline))

static volatile uint8_t one = 1;

static uint32_t bits(float f)
{
    union { float f; uint32_t u; } v;
    v.f = f;
    return v.u;
}

NOINLINE static float quo(float a, float b) { return a / b; }
NOINLINE static int32_t to_i32(float a) { return (int32_t)a; }
NOINLINE static uint16_t to_u16(float a) { return (uint16_t)a; }
NOINLINE static float from_i16(int16_t a) { return (float)a; }
NOINLINE static float from_u32(uint32_t a) { return (float)a; }

void main(void)
{
    float c = 1000000.0f;
    bench_init();
    if (one != 1) c = 0.0f;
    bench_result32("div", bits(quo(c, 3.25f)));
    bench_result32("third", bits(quo(1.0f, 3.0f)));
    bench_result32("toi", (uint32_t)to_i32(quo(c, -7.0f)));
    bench_result16("tou", to_u16(quo(c, 100.0f)));
    bench_result32("fromi", bits(from_i16(-12345)));
    bench_result32("fromu", bits(from_u32(4000000000UL)));
    bench_result32("round", (uint32_t)to_i32(quo(from_i16(22), from_i16(7))));
    bench_exit(to_i32(quo(c, 8.0f)) == 125000L && to_u16(from_i16(77)) == 77 ? 0 : 1);
}
