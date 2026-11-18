/* 4x4 matrix multiply with 16-bit signed elements. */
#include "bench.h"

#define N 4

static const int8_t init_a[N][N] = {
    { 1, -2, 3, 4 }, { 5, 6, -7, 8 }, { -9, 10, 11, 12 }, { 13, 14, 15, -16 },
};
static int16_t a[N][N], b[N][N], c[N][N];

static void multiply(void)
{
    for (uint8_t i = 0; i < N; i++) {
        for (uint8_t j = 0; j < N; j++) {
            int16_t s = 0;
            for (uint8_t k = 0; k < N; k++) {
                /* unsigned arithmetic: wraps instead of overflowing */
                s = (int16_t)((unsigned)s + (unsigned)a[i][k] * (unsigned)b[k][j]);
            }
            c[i][j] = s;
        }
    }
}

void main(void)
{
    int16_t trace = 0;
    uint16_t sum = 0;
    bench_init();
    for (uint8_t i = 0; i < N; i++) {
        for (uint8_t j = 0; j < N; j++) {
            a[i][j] = init_a[i][j];
            b[i][j] = (int16_t)(init_a[j][i] * 3 - (int16_t)i);
        }
    }
    multiply();
    /* Square the product once more: a = c, b = c. */
    for (uint8_t i = 0; i < N; i++) {
        for (uint8_t j = 0; j < N; j++) {
            a[i][j] = c[i][j];
            b[i][j] = (int16_t)(c[i][j] / 16);
        }
    }
    multiply();
    for (uint8_t i = 0; i < N; i++) {
        trace = (int16_t)((unsigned)trace + (unsigned)c[i][i]);
        for (uint8_t j = 0; j < N; j++) {
            sum = (uint16_t)(sum * 31u + (uint16_t)c[i][j]);
        }
    }
    bench_result16("trace", (uint16_t)trace);
    bench_result16("sum", sum);
    bench_exit(0);
}
