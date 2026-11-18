/* 32-bit add, multiply, divide and shifts. */
#include "bench.h"

static uint32_t lcg(uint32_t x)
{
    return x * 1664525UL + 1013904223UL;
}

static uint32_t mulmod(uint32_t a, uint32_t b, uint32_t m)
{
    uint32_t r = 0;
    a %= m;
    while (b) {
        if (b & 1) {
            r = (r + a) % m;
        }
        a = (a << 1) % m;
        b >>= 1;
    }
    return r;
}

void main(void)
{
    uint32_t x = 1, div = 0, pw = 1;
    int32_t s = 0;

    bench_init();
    for (uint8_t i = 0; i < 10; i++) {
        x = lcg(x);
    }
    for (uint32_t d = 3; d < 3000; d += 331) {
        div += 4000000000UL / d + 4000000000UL % d;
    }
    /* 7^20 mod 1000003, by repeated modular multiply. */
    for (uint8_t i = 0; i < 20; i++) {
        pw = mulmod(pw, 7, 1000003UL);
    }
    for (int16_t i = -5; i < 5; i++) {
        s += (int32_t)i * 100000L / 7 + ((int32_t)-100000L >> (i & 15));
    }
    bench_result32("lcg", x);
    bench_result32("div", div);
    bench_result32("pow", pw);
    bench_result32("signed", (uint32_t)s);
    /* x after ten steps of the Numerical Recipes generator from seed 1. */
    bench_exit(x == 0xA3A5A0E3UL && pw == 0x00081B26UL ? 0 : 1);
}
