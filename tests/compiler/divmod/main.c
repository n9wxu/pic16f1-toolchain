/* Division and remainder on every width, signed and unsigned: by run-time
 * values (the library routines), by constants, and by powers of two, which
 * the compiler turns into shifts. */
#include "bench.h"
#define NOINLINE __attribute__((noinline))
#define COUNT(a) ((uint8_t)(sizeof(a) / sizeof(a)[0]))

static const int8_t v8[] = { 0, 1, -1, 2, -2, 7, -7, 8, -8, 9, 15, -15, 16, -16, 17, -17,
                             63, 64, -64, 100, -100, 127, -127, -128 };
static const int16_t v16[] = { 0, 1, -1, 2, -2, 7, -7, 15, -15, 16, -16, 17, -17, 100, -100,
                               255, 256, -255, -256, -257, 1000, -1000, 4095, 4096, -4096,
                               -4097, 12345, -12345, 32767, -32767, -32768 };
static const int32_t v32[] = { 0, 1, -1, 3, -3, 15, -16, 17, 255, -256, 1000, -1000, 65535,
                               65536, -65536, -65537, 123456789, -123456789, 2147483647,
                               -2147483647, -2147483647 - 1 };

/* Run-time operands: these call the library routines. A quotient and a
 * remainder of the same operands share one call where there is a routine
 * that gives both. */
NOINLINE static uint8_t udiv8(uint8_t a, uint8_t b) { return (uint8_t)(a / b); }
NOINLINE static uint8_t umod8(uint8_t a, uint8_t b) { return (uint8_t)(a % b); }
NOINLINE static int8_t sdiv8(int8_t a, int8_t b) { return (int8_t)(a / b); }
NOINLINE static int8_t smod8(int8_t a, int8_t b) { return (int8_t)(a % b); }
NOINLINE static uint16_t udiv16(uint16_t a, uint16_t b) { return (uint16_t)(a / b); }
NOINLINE static uint16_t umod16(uint16_t a, uint16_t b) { return (uint16_t)(a % b); }
NOINLINE static int16_t sdiv16(int16_t a, int16_t b) { return (int16_t)(a / b); }
NOINLINE static int16_t smod16(int16_t a, int16_t b) { return (int16_t)(a % b); }
NOINLINE static uint32_t udiv32(uint32_t a, uint32_t b) { return a / b; }
NOINLINE static uint32_t umod32(uint32_t a, uint32_t b) { return a % b; }
NOINLINE static int32_t sdiv32(int32_t a, int32_t b) { return a / b; }
NOINLINE static int32_t smod32(int32_t a, int32_t b) { return a % b; }

NOINLINE static uint16_t both8(uint8_t a, uint8_t b)
{
    return (uint16_t)(((uint16_t)(uint8_t)(a / b) << 8) | (uint8_t)(a % b));
}
NOINLINE static uint16_t both16(uint16_t a, uint16_t b)
{
    return (uint16_t)((uint16_t)(a / b) * 3u + (uint16_t)(a % b));
}
NOINLINE static uint32_t both32(uint32_t a, uint32_t b) { return (a / b) * 3u + a % b; }

/* Constant divisors. */
#define S16(d) (uint16_t)((uint16_t)(int16_t)(x / (d)) + (uint16_t)(int16_t)(x % (d)))
NOINLINE static uint16_t sconst16(int16_t x)
{
    return (uint16_t)(S16(2) + S16(4) + S16(16) + S16(128) + S16(256) + S16(4096) + S16(16384) +
                      S16(-2) + S16(-16) + S16(-256) + S16(3) + S16(10) + S16(-10) + S16(1000));
}
#define U16(d) (uint16_t)((uint16_t)(u / (d)) + (uint16_t)(u % (d)))
NOINLINE static uint16_t uconst16(uint16_t u)
{
    return (uint16_t)(U16(2u) + U16(16u) + U16(256u) + U16(32768u) + U16(3u) + U16(10u) +
                      U16(1000u) + U16(65535u));
}
#define S8(d) (uint8_t)((uint8_t)(int8_t)(x / (d)) + (uint8_t)(int8_t)(x % (d)))
NOINLINE static uint8_t sconst8(int8_t x)
{
    return (uint8_t)(S8(2) + S8(8) + S8(64) + S8(-4) + S8(3) + S8(10));
}
#define S32(d) ((uint32_t)(x / (d)) + (uint32_t)(x % (d)))
NOINLINE static uint32_t sconst32(int32_t x)
{
    return S32(2) + S32(16) + S32(65536L) + S32(-256) + S32(10) + S32(1000000L);
}

void main(void)
{
    uint16_t h8 = 0, h16 = 0, hc = 0;
    uint32_t h32 = 0;
    bench_init();

    for (uint8_t i = 0; i < COUNT(v8); i++) {
        for (uint8_t j = 0; j < COUNT(v8); j++) {
            int8_t a = v8[i], b = v8[j];
            if (b == 0) continue;
            h8 = (uint16_t)(h8 * 31u + udiv8((uint8_t)a, (uint8_t)b));
            h8 = (uint16_t)(h8 * 31u + umod8((uint8_t)a, (uint8_t)b));
            h8 = (uint16_t)(h8 * 31u + both8((uint8_t)a, (uint8_t)b));
            if (a == -128 && b == -1) continue;
            h8 = (uint16_t)(h8 * 31u + (uint8_t)sdiv8(a, b));
            h8 = (uint16_t)(h8 * 31u + (uint8_t)smod8(a, b));
        }
        hc = (uint16_t)(hc * 31u + sconst8(v8[i]));
    }
    bench_result16("h8", h8);

    for (uint8_t i = 0; i < COUNT(v16); i++) {
        for (uint8_t j = 0; j < COUNT(v16); j++) {
            int16_t a = v16[i], b = v16[j];
            if (b == 0) continue;
            h16 = (uint16_t)(h16 * 31u + udiv16((uint16_t)a, (uint16_t)b));
            h16 = (uint16_t)(h16 * 31u + umod16((uint16_t)a, (uint16_t)b));
            h16 = (uint16_t)(h16 * 31u + both16((uint16_t)a, (uint16_t)b));
            if (a == -32768 && b == -1) continue;
            h16 = (uint16_t)(h16 * 31u + (uint16_t)sdiv16(a, b));
            h16 = (uint16_t)(h16 * 31u + (uint16_t)smod16(a, b));
        }
        hc = (uint16_t)(hc * 31u + sconst16(v16[i]));
        hc = (uint16_t)(hc * 31u + uconst16((uint16_t)v16[i]));
    }
    bench_result16("h16", h16);

    for (uint8_t i = 0; i < COUNT(v32); i++) {
        for (uint8_t j = 0; j < COUNT(v32); j++) {
            int32_t a = v32[i], b = v32[j];
            if (b == 0) continue;
            h32 = h32 * 31u + udiv32((uint32_t)a, (uint32_t)b);
            h32 = h32 * 31u + umod32((uint32_t)a, (uint32_t)b);
            h32 = h32 * 31u + both32((uint32_t)a, (uint32_t)b);
            if (a == -2147483647 - 1 && b == -1) continue;
            h32 = h32 * 31u + (uint32_t)sdiv32(a, b);
            h32 = h32 * 31u + (uint32_t)smod32(a, b);
        }
        h32 = h32 * 31u + sconst32(v32[i]);
    }
    bench_result32("h32", h32);
    bench_result16("hc", hc);
    bench_exit(0);
}
