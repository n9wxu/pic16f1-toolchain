/* Insertion sort of bytes and bubble sort of 16-bit words. */
#include "bench.h"

#define N8 32
#define N16 16

static uint8_t a8[N8];
static uint16_t a16[N16];

static void insertion_sort(uint8_t *a, uint8_t n)
{
    for (uint8_t i = 1; i < n; i++) {
        uint8_t key = a[i];
        uint8_t j = i;
        while (j > 0 && a[j - 1] > key) {
            a[j] = a[j - 1];
            j--;
        }
        a[j] = key;
    }
}

static void bubble_sort(uint16_t *a, uint8_t n)
{
    uint8_t swapped;
    do {
        swapped = 0;
        for (uint8_t i = 1; i < n; i++) {
            if (a[i - 1] > a[i]) {
                uint16_t t = a[i - 1];
                a[i - 1] = a[i];
                a[i] = t;
                swapped = 1;
            }
        }
        n--;
    } while (swapped);
}

void main(void)
{
    uint8_t seed = 0x5A;
    uint16_t seed16 = 0xACE1;
    uint16_t sum = 0;
    uint8_t ok = 1;

    bench_init();
    for (uint8_t i = 0; i < N8; i++) {
        seed = (uint8_t)(seed * 37u + 11u);
        a8[i] = seed;
    }
    for (uint8_t i = 0; i < N16; i++) {
        seed16 = (uint16_t)(seed16 * 25173u + 13849u);
        a16[i] = seed16;
    }
    insertion_sort(a8, N8);
    bubble_sort(a16, N16);
    for (uint8_t i = 1; i < N8; i++) {
        if (a8[i - 1] > a8[i]) ok = 0;
    }
    for (uint8_t i = 1; i < N16; i++) {
        if (a16[i - 1] > a16[i]) ok = 0;
    }
    /* Position-weighted sums change if any element is out of place. */
    for (uint8_t i = 0; i < N8; i++) {
        sum = (uint16_t)(sum + (uint16_t)a8[i] * (uint16_t)(i + 1));
    }
    bench_result16("sum8", sum);
    sum = 0;
    for (uint8_t i = 0; i < N16; i++) {
        sum = (uint16_t)(sum ^ (uint16_t)(a16[i] + i));
    }
    bench_result16("mix16", sum);
    bench_result16("min8", a8[0]);
    bench_result16("max16", a16[N16 - 1]);
    bench_exit(ok ? 0 : 1);
}
