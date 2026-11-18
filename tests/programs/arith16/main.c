/* 8- and 16-bit multiply, divide, modulo and variable shifts. */
#include "bench.h"

static uint16_t gcd(uint16_t a, uint16_t b)
{
    while (b) {
        uint16_t t = (uint16_t)(a % b);
        a = b;
        b = t;
    }
    return a;
}

static uint16_t isqrt(uint16_t n)
{
    uint16_t r = 0;
    while ((uint16_t)((r + 1) * (r + 1)) <= n && r < 255) {
        r++;
    }
    return r;
}

void main(void)
{
    uint16_t squares = 0, mix = 0, shifts = 0;
    int16_t signed_acc = 0;
    uint8_t ok;

    bench_init();
    for (uint8_t i = 0; i < 100; i++) {
        squares = (uint16_t)(squares + (uint16_t)i * i);
    }
    for (uint16_t i = 1; i < 60; i++) {
        mix = (uint16_t)(mix + (uint16_t)(50000u / i) + (uint16_t)(50000u % i));
    }
    for (int8_t i = -20; i < 20; i++) {
        signed_acc = (int16_t)(signed_acc + (int16_t)((int16_t)i * 37) / 5 + (int16_t)(i * 11) % 7);
    }
    for (uint8_t i = 0; i < 16; i++) {
        shifts = (uint16_t)(shifts ^ (uint16_t)(0xA5C3u << i) ^ (uint16_t)(0xA5C3u >> i));
        shifts = (uint16_t)(shifts + (uint16_t)((int16_t)-12345 >> (i & 7)));
    }
    bench_result16("squares", squares);
    bench_result16("mix", mix);
    bench_result16("signed", (uint16_t)signed_acc);
    bench_result16("shifts", shifts);
    bench_result16("gcd", gcd(1071, 462));
    bench_result16("isqrt", isqrt(40000u));
    ok = squares == 0x029E && gcd(1071, 462) == 21 && isqrt(40000u) == 200;
    bench_exit(ok ? 0 : 1);
}
