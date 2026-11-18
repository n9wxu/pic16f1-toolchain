/* Shifts by every amount, constant and run-time, on every width. */
#include "bench.h"
#define NOINLINE __attribute__((noinline))

NOINLINE static uint8_t shl8(uint8_t v, uint8_t n) { return (uint8_t)(v << n); }
NOINLINE static uint8_t shr8(uint8_t v, uint8_t n) { return (uint8_t)(v >> n); }
NOINLINE static int8_t sar8(int8_t v, uint8_t n) { return (int8_t)(v >> n); }
NOINLINE static uint16_t shl16(uint16_t v, uint8_t n) { return (uint16_t)(v << n); }
NOINLINE static uint16_t shr16(uint16_t v, uint8_t n) { return (uint16_t)(v >> n); }
NOINLINE static int16_t sar16(int16_t v, uint8_t n) { return (int16_t)(v >> n); }
NOINLINE static uint32_t shl32(uint32_t v, uint8_t n) { return v << n; }
NOINLINE static uint32_t shr32(uint32_t v, uint8_t n) { return v >> n; }
NOINLINE static int32_t sar32(int32_t v, uint8_t n) { return v >> n; }

#define CONST16(n) (uint16_t)((uint16_t)(x16 << (n)) ^ (uint16_t)(x16 >> (n)) ^ (uint16_t)(s16 >> (n)))
#define CONST32(n) ((x32 << (n)) ^ (x32 >> (n)) ^ (uint32_t)(s32 >> (n)))

NOINLINE static uint16_t const16(uint16_t x16, int16_t s16)
{
    return (uint16_t)(CONST16(1) + CONST16(2) + CONST16(3) + CONST16(4) + CONST16(5) + CONST16(6) +
                      CONST16(7) + CONST16(8) + CONST16(9) + CONST16(10) + CONST16(11) + CONST16(12) +
                      CONST16(13) + CONST16(14) + CONST16(15));
}

NOINLINE static uint32_t const32(uint32_t x32, int32_t s32)
{
    return CONST32(1) + CONST32(4) + CONST32(7) + CONST32(8) + CONST32(9) + CONST32(15) + CONST32(16) +
           CONST32(17) + CONST32(23) + CONST32(24) + CONST32(25) + CONST32(31);
}

NOINLINE static uint16_t rotl16(uint16_t v, uint8_t n)
{
    n &= 15;
    return (uint16_t)((v << n) | (v >> ((16 - n) & 15)));
}

void main(void)
{
    uint16_t h8 = 0, h16 = 0, hr = 0;
    uint32_t h32 = 0;
    bench_init();
    for (uint8_t n = 0; n < 8; n++) {
        h8 = (uint16_t)(h8 * 7u + shl8(0xB5, n) + shr8(0xB5, n) + (uint8_t)sar8((int8_t)0xB5, n));
    }
    for (uint8_t n = 0; n < 16; n++) {
        h16 = (uint16_t)(h16 * 7u + shl16(0xA5C3u, n) + shr16(0xA5C3u, n) + (uint16_t)sar16((int16_t)0xA5C3u, n));
        hr = (uint16_t)(hr * 3u + rotl16(0x8421u, n));
    }
    for (uint8_t n = 0; n < 32; n++) {
        h32 = h32 * 7u + shl32(0x8765A5C3UL, n) + shr32(0x8765A5C3UL, n) +
              (uint32_t)sar32((int32_t)0x8765A5C3UL, n);
    }
    bench_result16("var8", h8);
    bench_result16("var16", h16);
    bench_result32("var32", h32);
    bench_result16("rot", hr);
    bench_result16("const16", const16(0xA5C3u, (int16_t)0xA5C3u));
    bench_result32("const32", const32(0x8765A5C3UL, (int32_t)0x8765A5C3UL));
    bench_exit(shl16(1, 15) == 0x8000u && sar16(-32768, 15) == -1 ? 0 : 1);
}
