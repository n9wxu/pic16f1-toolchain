/* An interrupt that arrives deep in a recursion, with a handler that makes
 * a call: for the debugger to show the call stack through the handler and
 * the interrupted functions' variables. For scripts/test-debug.sh. */
#include <xc.h>

volatile unsigned char ticks;

__attribute__((noinline)) static void tick(void)
{
    ticks++;                    /* a breakpoint here is in the handler */
}

void __attribute__((interrupt)) isr(void)
{
    if (INTCONbits.TMR0IF) {
        INTCONbits.TMR0IF = 0;
        tick();
    }
}

static unsigned down(unsigned char n)
{
    volatile unsigned char here = n;
    if (n == 0) {
        /* Start the timer and wait here at the bottom for it. */
        INTCONbits.TMR0IE = 1;
        INTCONbits.GIE = 1;
        while (ticks == 0)
            ;
        INTCONbits.GIE = 0;
        return 0;
    }
    return down(n - 1) + here;
}

void main(void)
{
    volatile unsigned char depth = 4;
    unsigned total;
    OPTION_REG = 0x88;          /* timer 0 from the instruction clock */
    TMR0 = 0;
    INTCONbits.TMR0IF = 0;
    total = down(depth);
    LATD = total + ticks * 16;  /* 10 + 16 */
    SLEEP();
}
