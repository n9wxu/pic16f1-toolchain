/* Bit counting and reversal, and single-bit operations on a port latch. */
#include "bench.h"

static uint8_t popcount8(uint8_t v)
{
    uint8_t n = 0;
    while (v) {
        v &= (uint8_t)(v - 1);
        n++;
    }
    return n;
}

static uint8_t reverse8(uint8_t v)
{
    uint8_t r = 0;
    for (uint8_t i = 0; i < 8; i++) {
        r = (uint8_t)((r << 1) | (v & 1));
        v >>= 1;
    }
    return r;
}

static uint8_t parity16(uint16_t v)
{
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (uint8_t)(v & 1);
}

void main(void)
{
    uint16_t pop = 0, rev = 0, par = 0;
    uint8_t latch;
    bench_init();
    for (uint16_t i = 0; i < 256; i++) {
        pop = (uint16_t)(pop + popcount8((uint8_t)i));
        rev = (uint16_t)(rev + (uint16_t)reverse8((uint8_t)i) * (uint16_t)(i & 3));
        par = (uint16_t)(par + parity16((uint16_t)(i * 257u + 1u)));
    }
    TRISB = 0x00;
    LATB = 0x00;
    LATBbits.LATB0 = 1;
    LATBbits.LATB7 = 1;
    LATBbits.LATB3 = 1;
    LATBbits.LATB0 = 0;
    LATB ^= 0x18;
    latch = LATB;
    if (PORTBbits.RB7) {
        latch |= 0x01;
    }
    bench_result16("pop", pop);
    bench_result16("rev", rev);
    bench_result16("par", par);
    bench_result16("latch", latch);
    /* 256 bytes hold 1024 set bits; latch = 0x90 | 0x01 */
    bench_exit(pop == 1024 && latch == 0x91 ? 0 : 1);
}
