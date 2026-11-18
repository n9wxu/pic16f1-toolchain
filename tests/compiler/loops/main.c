/* Loop shapes: counting up and down, 8- and 16-bit counters, break,
 * continue, nesting, and unsigned wrap-around. */
#include "bench.h"
#define NOINLINE __attribute__((noinline))

static volatile uint8_t n8 = 200;
static volatile uint16_t n16 = 1000;

NOINLINE static uint16_t up8(uint8_t n)
{
    uint16_t s = 0;
    for (uint8_t i = 0; i < n; i++) s = (uint16_t)(s + i);
    return s;
}

NOINLINE static uint16_t down8(uint8_t n)
{
    uint16_t s = 0;
    for (uint8_t i = n; i != 0; i--) s = (uint16_t)(s ^ (uint16_t)(i * 259u));
    return s;
}

NOINLINE static uint32_t up16(uint16_t n)
{
    uint32_t s = 0;
    for (uint16_t i = 0; i <= n; i += 3) s += i;
    return s;
}

NOINLINE static uint16_t wrap(void)
{
    uint16_t count = 0;
    uint8_t i = 250;
    do {
        i = (uint8_t)(i + 7);
        count++;
    } while (i >= 7);            /* stops after the counter wraps */
    return (uint16_t)(count * 256u + i);
}

NOINLINE static uint16_t signed_down(int8_t start)
{
    uint16_t s = 0;
    for (int8_t i = start; i >= -5; i--) s = (uint16_t)(s * 3u + (uint8_t)i);
    return s;
}

NOINLINE static uint16_t control(uint8_t n)
{
    uint16_t s = 0;
    for (uint8_t i = 0; i < n; i++) {
        if ((i & 3) == 1) continue;
        for (uint8_t j = 0; j < 10; j++) {
            if (j == (i & 7)) break;
            if ((uint8_t)(i + j) % 5 == 0) continue;
            s = (uint16_t)(s + i * j);
        }
        if (s > 20000) break;
    }
    return s;
}

NOINLINE static uint16_t collatz(uint16_t v)
{
    uint16_t steps = 0;
    while (v != 1) {
        v = (v & 1) ? (uint16_t)(v * 3u + 1u) : (uint16_t)(v >> 1);
        steps++;
    }
    return steps;
}

void main(void)
{
    bench_init();
    bench_result16("up8", up8(n8));
    bench_result16("down8", down8(n8));
    bench_result32("up16", up16(n16));
    bench_result16("wrap", wrap());
    bench_result16("sdown", signed_down(5));
    bench_result16("control", control(n8));
    bench_result16("collatz", collatz(27));
    bench_exit(up8(200) == 19900 && collatz(27) == 111 ? 0 : 1);
}
