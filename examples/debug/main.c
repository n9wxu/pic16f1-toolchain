/* A small program to try the debugger on: see README.md beside it. */
#include <xc.h>

/* Configuration words, for running on a real PIC16F1937: internal
 * oscillator, watchdog off, MCLR and low-voltage programming on. (Erased
 * configuration words select an external clock, and the part does not
 * run.) The simulator does not need them. */
PIC_CONFIG_WORDS(0x09C4, 0x3EFF);

volatile unsigned char gain = 5;
unsigned total;
static unsigned char samples[8];

static unsigned char scale(unsigned char x, unsigned char k)
{
    unsigned char t = x * k;
    return t + 1;
}

static void fill(unsigned char n)
{
    unsigned char i;
    for (i = 0; i < n; i++) {
        samples[i] = scale(i, gain);
        total += samples[i];
    }
}

/* Recursion: these calls get stack frames. (The volatile variables keep
 * the optimizer from turning the recursion into a loop.) */
volatile unsigned char depth = 5;

static unsigned sum_to(unsigned char n)
{
    volatile unsigned char here = n;
    if (n == 0)
        return 0;
    return sum_to(n - 1) + here;
}

void main(void)
{
    unsigned char rounds = 2;
    while (rounds--)
        fill(4);
    total += sum_to(depth);
    LATD = total;       /* the program's exit code on the simulator */
    /* The simulator takes SLEEP as the end of the program. A real part
     * that has gone to sleep cannot be halted through the debug link, so
     * on hardware stop before this line. */
    SLEEP();
}
