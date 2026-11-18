/* An interrupt handler that calls functions and does 16- and 32-bit work
 * while the main loop computes checksums. Any register the handler fails
 * to save shows up as a wrong checksum. */
#include "bench.h"
#define NOINLINE __attribute__((noinline))

static volatile uint16_t ticks;
static volatile uint32_t isr_acc = 1;
static uint8_t data[40];

NOINLINE static uint32_t mix(uint32_t v, uint16_t t)
{
    return (v << 3) ^ (v >> 5) ^ t;
}

NOINLINE static uint16_t scale(uint16_t t)
{
    return (uint16_t)(t * 13u + (t >> 2));
}

BENCH_ISR(isr)
{
    if (INTCONbits.TMR0IF) {
        INTCONbits.TMR0IF = 0;
        ticks++;
        isr_acc = mix(isr_acc, scale(ticks));
    }
}

NOINLINE static uint16_t crc16(const uint8_t *p, uint8_t n)
{
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= (uint16_t)((uint16_t)*p++ << 8);
        for (uint8_t i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

NOINLINE static uint32_t sum32(const uint8_t *p, uint8_t n)
{
    uint32_t s = 0;
    while (n--) s = s * 65599UL + *p++;
    return s;
}

void main(void)
{
    uint16_t first_crc, bad = 0;
    uint32_t first_sum;
    uint8_t rounds = 0;
    bench_init();
    for (uint8_t i = 0; i < 40; i++) data[i] = (uint8_t)(i * 29u + 5u);
    first_crc = crc16(data, 40);
    first_sum = sum32(data, 40);

    OPTION_REG = 0x83;      /* Timer0 from Fosc/4, prescaler 1:16: a tick every 4096 cycles */
    TMR0 = 0;
    INTCONbits.TMR0IF = 0;
    INTCONbits.TMR0IE = 1;
    INTCONbits.GIE = 1;
    while (ticks < 40) {
        if (crc16(data, 40) != first_crc) bad++;
        if (sum32(data, 40) != first_sum) bad++;
        rounds++;
    }
    INTCONbits.GIE = 0;

    bench_result16("bad", bad);
    bench_puts(rounds > 0 && ticks >= 40 ? "interrupted\n" : "not interrupted\n");
    bench_exit(bad == 0 ? 0 : 1);
}
