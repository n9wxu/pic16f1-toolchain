/* Every comparison on every integer width, at the values where signed and
 * unsigned orderings differ. */
#include "bench.h"
#define NOINLINE __attribute__((noinline))

#define CMP(T, name)                                                           \
    NOINLINE static uint8_t name(T a, T b)                                     \
    {                                                                          \
        uint8_t r = 0;                                                         \
        if (a == b) r |= 1;                                                    \
        if (a != b) r |= 2;                                                    \
        if (a < b) r |= 4;                                                     \
        if (a <= b) r |= 8;                                                    \
        if (a > b) r |= 16;                                                    \
        if (a >= b) r |= 32;                                                   \
        return r;                                                              \
    }

CMP(int8_t, cmp_s8)
CMP(uint8_t, cmp_u8)
CMP(int16_t, cmp_s16)
CMP(uint16_t, cmp_u16)
CMP(int32_t, cmp_s32)
CMP(uint32_t, cmp_u32)

/* Comparisons against constants take different code paths. */
NOINLINE static uint8_t consts_s8(int8_t a)
{
    return (uint8_t)((a < 0) | ((a >= 0) << 1) | ((a > 5) << 2) | ((a <= -5) << 3) |
                     ((a == -128) << 4) | ((a < -127) << 5) | ((a > 126) << 6) | ((a != 0) << 7));
}

NOINLINE static uint8_t consts_s16(int16_t a)
{
    return (uint8_t)((a < 0) | ((a >= 0) << 1) | ((a > 300) << 2) | ((a <= -300) << 3) |
                     ((a == 256) << 4) | ((a < 256) << 5) | ((a > -32768) << 6) | ((a != -1) << 7));
}

NOINLINE static uint8_t consts_u16(uint16_t a)
{
    return (uint8_t)((a < 256) | ((a >= 0x8000u) << 1) | ((a > 0xFF00u) << 2) | ((a <= 1) << 3) |
                     ((a == 0) << 4) | ((a != 0xFFFFu) << 5) | ((a > 255) << 6) | ((a < 0x100u) << 7));
}

static const int16_t v16[] = { 0, 1, -1, 127, 128, 255, 256, -256, 32767, -32768, 300, -300 };
static const int32_t v32[] = { 0, 1, -1, 65535L, 65536L, -65536L, 2147483647L, -2147483647L - 1, 0x00FF0000L };

void main(void)
{
    uint16_t h8 = 0, h16 = 0, h32 = 0, hc = 0;
    bench_init();
    for (uint8_t i = 0; i < 12; i++) {
        for (uint8_t j = 0; j < 12; j++) {
            h8 = (uint16_t)(h8 * 31u + cmp_s8((int8_t)v16[i], (int8_t)v16[j]));
            h8 = (uint16_t)(h8 * 31u + cmp_u8((uint8_t)v16[i], (uint8_t)v16[j]));
            h16 = (uint16_t)(h16 * 31u + cmp_s16(v16[i], v16[j]));
            h16 = (uint16_t)(h16 * 31u + cmp_u16((uint16_t)v16[i], (uint16_t)v16[j]));
        }
        hc = (uint16_t)(hc * 31u + consts_s8((int8_t)v16[i]));
        hc = (uint16_t)(hc * 31u + consts_s16(v16[i]));
        hc = (uint16_t)(hc * 31u + consts_u16((uint16_t)v16[i]));
    }
    for (uint8_t i = 0; i < 9; i++) {
        for (uint8_t j = 0; j < 9; j++) {
            h32 = (uint16_t)(h32 * 31u + cmp_s32(v32[i], v32[j]));
            h32 = (uint16_t)(h32 * 31u + cmp_u32((uint32_t)v32[i], (uint32_t)v32[j]));
        }
    }
    bench_result16("h8", h8);
    bench_result16("h16", h16);
    bench_result16("h32", h32);
    bench_result16("const", hc);
    bench_exit(cmp_s8(-1, 1) == (2 | 4 | 8) && cmp_u8(255, 1) == (2 | 16 | 32) ? 0 : 1);
}
