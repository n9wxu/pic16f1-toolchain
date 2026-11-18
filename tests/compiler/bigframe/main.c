/* Stack frames larger than the reach of one FSR offset (32 bytes). */
#include "bench.h"
#define NOINLINE __attribute__((noinline))

static volatile uint8_t one = 1;

NOINLINE static uint16_t fill_sum(uint8_t *p, uint8_t n, uint8_t seed)
{
    uint16_t s = 0;
    for (uint8_t i = 0; i < n; i++) {
        p[i] = (uint8_t)(seed + i * 3u);
        s = (uint16_t)(s + p[i]);
    }
    return s;
}

NOINLINE static uint16_t inner(uint8_t seed)
{
    uint8_t buf[70];
    uint16_t words[10];
    uint16_t s = fill_sum(buf, 70, seed);
    for (uint8_t i = 0; i < 10; i++) words[i] = (uint16_t)(buf[i * 7u] * 257u);
    for (uint8_t i = 0; i < 10; i++) s ^= words[9 - i];
    return s;
}

NOINLINE static uint16_t outer(uint8_t seed)
{
    uint8_t a[100];
    uint32_t wide[6];
    uint16_t s = fill_sum(a, 100, seed);
    uint16_t t;
    for (uint8_t i = 0; i < 6; i++) wide[i] = (uint32_t)a[i * 16u] * 100000UL + i;
    t = inner((uint8_t)(seed + 1));
    /* The arrays must have survived the nested call. */
    for (uint8_t i = 0; i < 100; i++) s = (uint16_t)(s + a[i] * (uint16_t)(i & 3));
    for (uint8_t i = 0; i < 6; i++) s = (uint16_t)(s ^ (uint16_t)(wide[i] >> 8));
    return (uint16_t)(s + t);
}

void main(void)
{
    uint16_t r;
    bench_init();
    r = outer((uint8_t)(6 + one));
    bench_result16("outer", r);
    bench_result16("inner", inner(3));
    bench_exit(0);
}
