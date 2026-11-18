#include "bench.h"

/* Internal oscillator, watchdog off, MCLR enabled, low-voltage programming off. */
#if defined(__XC8)
#pragma config FOSC = INTOSC, WDTE = OFF, PWRTE = OFF, MCLRE = ON, CP = OFF
#pragma config CPD = OFF, BOREN = ON, CLKOUTEN = OFF, IESO = OFF, FCMEN = OFF
#pragma config WRT = OFF, VCAPEN = OFF, PLLEN = OFF, STVREN = ON, BORV = LO, LVP = OFF
#else
PIC_CONFIG_WORDS(0x0FE4, 0x1EFF);
#endif

void bench_init(void)
{
    OSCCON = 0x70;          /* 8 MHz internal oscillator */
    LATD = 0xFF;            /* exit-code port: 0xFF while running */
    TRISD = 0x00;
    SP1BRGL = 51;           /* 9600 baud at 8 MHz with BRGH = 1 */
    TXSTAbits.BRGH = 1;
    RCSTAbits.SPEN = 1;
    TXSTAbits.TXEN = 1;
}

void bench_putc(char c)
{
    while (!PIR1bits.TXIF) {
    }
    TXREG = (uint8_t)c;
}

void bench_puts(const char *s)
{
    while (*s) {
        bench_putc(*s++);
    }
}

static void hex4(uint8_t v)
{
    v &= 0x0F;
    bench_putc((char)(v < 10 ? '0' + v : 'A' + (v - 10)));
}

void bench_hex8(uint8_t v)
{
    hex4((uint8_t)(v >> 4));
    hex4(v);
}

void bench_hex16(uint16_t v)
{
    bench_hex8((uint8_t)(v >> 8));
    bench_hex8((uint8_t)v);
}

void bench_hex32(uint32_t v)
{
    bench_hex16((uint16_t)(v >> 16));
    bench_hex16((uint16_t)v);
}

void bench_result16(const char *name, uint16_t v)
{
    bench_puts(name);
    bench_putc('=');
    bench_hex16(v);
    bench_putc('\n');
}

void bench_result32(const char *name, uint32_t v)
{
    bench_puts(name);
    bench_putc('=');
    bench_hex32(v);
    bench_putc('\n');
}

void bench_exit(uint8_t code)
{
    while (!TXSTAbits.TRMT) {
    }
    LATD = code;
    for (;;) {
        SLEEP();
    }
}
