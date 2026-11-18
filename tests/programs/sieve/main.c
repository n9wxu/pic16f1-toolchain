/* Sieve of Eratosthenes over 0..255 in a 32-byte bit array. 54 primes, last 251. */
#include "bench.h"

static uint8_t composite[32];

static uint8_t test(uint8_t n)
{
    return (uint8_t)(composite[n >> 3] & (uint8_t)(1u << (n & 7)));
}

static void set(uint8_t n)
{
    composite[n >> 3] |= (uint8_t)(1u << (n & 7));
}

void main(void)
{
    uint8_t count = 0, last = 0;
    bench_init();
    for (uint8_t i = 2; i < 16; i++) {
        if (!test(i)) {
            for (uint16_t j = (uint16_t)i * i; j < 256; j += i) {
                set((uint8_t)j);
            }
        }
    }
    for (uint16_t i = 2; i < 256; i++) {
        if (!test((uint8_t)i)) {
            count++;
            last = (uint8_t)i;
        }
    }
    bench_result16("count", count);
    bench_result16("last", last);
    bench_exit(count == 54 && last == 251 ? 0 : 1);
}
