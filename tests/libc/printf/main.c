/* printf with integers and floating point, on the PIC16F18877: for
 * scripts/test-libc.sh, which builds it with each -mprintf= kind. */
#include <stdio.h>
#include <xc.h>
static int put(char c, FILE *f) { (void)f; while (!PIR3bits.TXIF) { } TX1REG = c; return c; }
static FILE out = FDEV_SETUP_STREAM(put, NULL, NULL, _FDEV_SETUP_WRITE);
FILE *const stdout = &out;
volatile float x = 3.14159f; volatile int n = 42; volatile long big = 123456789;
void main(void)
{
    SP1BRGL = 51; TX1STAbits.BRGH = 1; RC1STAbits.SPEN = 1; TX1STAbits.TXEN = 1;
    printf("n=%d big=%ld x=%.3f e=%g\n", n, big, (double)x, (double)(x * 1000.0f));
    LATD = 0; SLEEP();
}
