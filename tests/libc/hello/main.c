#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <xc.h>

static int uart_putc(char c, FILE *f)
{
    (void)f;
    while (!PIR1bits.TXIF) { }
    TXREG = (unsigned char)c;
    return (unsigned char)c;
}
static FILE uart = FDEV_SETUP_STREAM(uart_putc, NULL, NULL, _FDEV_SETUP_WRITE);
FILE *const stdout = &uart;

void exit(int code) { LATD = (unsigned char)code; for (;;) SLEEP(); }

int main(void)
{
    char buf[16];
    SP1BRGL = 51; TXSTAbits.BRGH = 1; RCSTAbits.SPEN = 1; TXSTAbits.TXEN = 1;
    printf("hello %s, %d + %u = %x\n", "picolibc", -5, 1937u, 0xBEEF);
    strcpy(buf, "abc");
    strcat(buf, "def");
    printf("%s %u %d\n", buf, (unsigned)strlen(buf), atoi("-321") + abs(-21));
    return 0;
}
