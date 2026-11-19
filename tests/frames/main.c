/* Which frame each function of a.c gets is the linker's decision; see
 * scripts/test-frames.sh for what is expected. The program also has to give
 * the right answers with those frames. */
#include "bench.h"

uint8_t plain(uint8_t x);
uint8_t shared(uint8_t x);
uint8_t odd(uint8_t n);
uint8_t through_pointer(uint8_t x);
uint8_t reenters(uint8_t (*f)(uint8_t), uint8_t n);

uint8_t depth;
static volatile uint8_t ticks, from_isr;

void fill(uint8_t *p, uint8_t v)
{
    for (uint8_t i = 0; i < 4; i++)
        p[i] = (uint8_t)(v + i);
}

uint8_t even(uint8_t n)
{
    uint8_t v[4];
    fill(v, n);
    depth++;
    return n ? odd((uint8_t)(n - 1)) : (uint8_t)(v[0] | 1);
}

static uint8_t again(uint8_t n) { return reenters(again, n); }

BENCH_ISR(isr)
{
    if (INTCONbits.TMR0IF) {
        INTCONbits.TMR0IF = 0;
        from_isr = shared(ticks);
        ticks++;
    }
}

void main(void)
{
    uint8_t (*volatile fp)(uint8_t) = through_pointer;
    uint16_t sum = 0;
    bench_init();
    OPTION_REG = 0x80;
    TMR0 = 0;
    INTCONbits.TMR0IF = 0;
    INTCONbits.TMR0IE = 1;
    INTCONbits.GIE = 1;
    /* shared() runs here and in the handler at the same time. */
    while (ticks < 5)
        sum = (uint16_t)(sum + shared(7));
    INTCONbits.GIE = 0;
    bench_result16("shared", (uint16_t)(shared(7) == 1 && sum % 1 == 0 && sum != 0));
    bench_result16("plain", plain(10));
    bench_result16("even", even(6));
    bench_result16("depth", depth);
    bench_result16("pointer", fp(4));
    bench_result16("again", again(3));
    bench_exit(0);
}
