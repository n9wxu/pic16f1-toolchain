/* Harness for tests that use the C library (picolibc): standard output goes
 * to the EUSART, exit(n) and a return from main report n on the exit port,
 * and abort() reports 134. Works on every device in the database; register
 * names differ between families. */
#include <stdio.h>
#include <xc.h>

#if defined(__PIC16F1937__)
PIC_CONFIG_WORDS(0x0FE4, 0x1EFF);
#endif

#if defined(TX1REG)
#define UART_TX TX1REG
#define UART_TXSTA TX1STAbits
#define UART_RCSTA RC1STAbits
#else
#define UART_TX TXREG
#define UART_TXSTA TXSTAbits
#define UART_RCSTA RCSTAbits
#endif

#if defined(LATD)
#define EXIT_LAT LATD
#define EXIT_TRIS TRISD
#else
#define EXIT_LAT LATC
#define EXIT_TRIS TRISC
#endif

static int uart_putc(char c, FILE *file)
{
    (void)file;
    if (!UART_TXSTA.TXEN) {
        SP1BRGL = 51;
        UART_TXSTA.BRGH = 1;
        UART_RCSTA.SPEN = 1;
        UART_TXSTA.TXEN = 1;
    }
    while (!UART_TXSTA.TRMT) {
    }
    UART_TX = (unsigned char)c;
    return (unsigned char)c;
}

static FILE uart = FDEV_SETUP_STREAM(uart_putc, NULL, NULL, _FDEV_SETUP_WRITE);
FILE *const stdout = &uart;
FILE *const stderr = &uart;

__attribute__((noreturn)) void _exit(int code)
{
    EXIT_TRIS = 0;
    EXIT_LAT = (unsigned char)code;
    for (;;) {
        SLEEP();
    }
}
