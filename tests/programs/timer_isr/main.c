/* Timer0 overflow interrupt: count ten ticks while the main loop keeps working. */
#include "bench.h"

static volatile uint8_t ticks;
static volatile uint16_t shared;

BENCH_ISR(isr)
{
    if (INTCONbits.TMR0IF) {
        INTCONbits.TMR0IF = 0;
        ticks++;
        shared = (uint16_t)(shared + 3);
    }
}

void main(void)
{
    uint16_t work = 0;
    uint16_t snapshot;
    bench_init();
    OPTION_REG = 0x80;      /* Timer0 from Fosc/4, prescaler 1:2 */
    TMR0 = 0;
    INTCONbits.TMR0IF = 0;
    INTCONbits.TMR0IE = 1;
    INTCONbits.GIE = 1;
    while (ticks < 10) {
        work = (uint16_t)(work * 5u + 1u);
    }
    INTCONbits.GIE = 0;
    snapshot = shared;
    bench_result16("ticks", ticks);
    bench_result16("shared", snapshot);
    bench_puts(work != 0 ? "worked\n" : "idle\n");
    bench_exit(ticks == 10 && snapshot == 30 ? 0 : 1);
}
