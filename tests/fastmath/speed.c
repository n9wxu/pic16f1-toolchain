/* Arithmetic to time: 32-bit multiplication and division, and float
 * multiplication, division and addition. scripts/test-fastmath.sh runs it
 * with the standard and with the fast library and compares the cycles;
 * the results must be the same with both, and are checked. */
#include <xc.h>
volatile unsigned long seed = 1;
volatile float scale = 1.0009765625f;
void main(void)
{
    unsigned long x = seed, sum = 0;
    float f = 1.0f, g = 0.0f;
    unsigned int i;
    for (i = 0; i < 200; i++) {
        x = x * 1103515245UL + 12345UL;
        sum += x / (i + 3) + x % 1000u + x / 70000UL;
    }
    for (i = 1; i <= 200; i++) {
        f = f * scale;
        g += f / (float)i;
    }
    /* 0x5A if all of it came out as it should. */
    LATD = (sum == 0x44035D0DUL && (unsigned long)(g * 1000.0f) == 0x17C3UL) ? 0x5A : 1;
    SLEEP();
}
