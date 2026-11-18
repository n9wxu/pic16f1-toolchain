/* Recursion: every call level needs its own frame on the software stack. */
#include "bench.h"
#define NOINLINE __attribute__((noinline))

static volatile uint8_t seed = 10;

NOINLINE static uint16_t fib(uint8_t n)
{
    if (n < 2) return n;
    return (uint16_t)(fib((uint8_t)(n - 1)) + fib((uint8_t)(n - 2)));
}

NOINLINE static uint32_t fact(uint8_t n)
{
    return n <= 1 ? 1UL : (uint32_t)n * fact((uint8_t)(n - 1));
}

NOINLINE static uint16_t ack(uint8_t m, uint16_t n)
{
    if (m == 0) return (uint16_t)(n + 1);
    if (n == 0) return ack((uint8_t)(m - 1), 1);
    return ack((uint8_t)(m - 1), ack(m, (uint16_t)(n - 1)));
}

static uint8_t is_odd(uint8_t n);
NOINLINE static uint8_t is_even(uint8_t n) { return n == 0 ? 1 : is_odd((uint8_t)(n - 1)); }
NOINLINE static uint8_t is_odd(uint8_t n) { return n == 0 ? 0 : is_even((uint8_t)(n - 1)); }

static uint16_t moves;
NOINLINE static void hanoi(uint8_t n, uint8_t from, uint8_t to, uint8_t via)
{
    if (n == 0) return;
    hanoi((uint8_t)(n - 1), from, via, to);
    moves = (uint16_t)(moves + from * 3u + to);
    hanoi((uint8_t)(n - 1), via, to, from);
}

/* Recursion with a local array in each frame. */
NOINLINE static uint16_t depth_sum(uint8_t n)
{
    uint8_t local[6];
    uint16_t s = 0;
    for (uint8_t i = 0; i < 6; i++) local[i] = (uint8_t)(n + i);
    if (n) s = depth_sum((uint8_t)(n - 1));
    for (uint8_t i = 0; i < 6; i++) s = (uint16_t)(s + local[i]);
    return s;
}

void main(void)
{
    uint8_t n = seed;
    bench_init();
    bench_result16("fib", fib(n));
    bench_result32("fact", fact((uint8_t)(n - 2)));
    bench_result16("ack", ack(2, 2));
    bench_result16("even", (uint16_t)((is_even(n) << 1) | is_even((uint8_t)(n + 1))));
    hanoi(5, 1, 3, 2);
    bench_result16("hanoi", moves);
    bench_result16("depth", depth_sum(8));
    bench_exit(fib(n) == 55 && fact(8) == 40320UL && ack(2, 2) == 7 ? 0 : 1);
}
