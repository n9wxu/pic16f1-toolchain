/* A program that builds for every supported device without change: toggle
 * an output, and use an array large enough to reach the device's upper
 * banks, so that banked addressing is exercised across the whole RAM.
 *
 * It uses no UART. The result is the exit code on the exit port: 0 for
 * success. Configuration words are left at their defaults, which is fine for
 * the simulator; a version for real hardware would set them. */
#include <xc.h>
#include <stdint.h>

#if defined(LATD)
#define EXIT_LAT LATD
#define EXIT_TRIS TRISD
#else
#define EXIT_LAT LATC
#define EXIT_TRIS TRISC
#endif

/* About three quarters of each device's RAM. */
#if defined(__PIC16F18877__)
#define N 3000
#elif defined(__PIC16F15276__)
#define N 1500
#elif defined(__PIC16F1937__)
#define N 360
#else
#define N 750
#endif

static uint8_t big[N];
static volatile uint16_t toggles;
static uint16_t marker = 0xBEEF;      /* initialised data, copied from flash */

static void delay(uint8_t n)
{
    while (n--) {
        NOP();
    }
}

void main(void)
{
    uint16_t by_index = 0, by_pointer = 0;
    uint8_t code = 0;

    EXIT_LAT = 0xFF;
    EXIT_TRIS = 0;
    TRISA = 0;
    LATA = 0;
    for (uint8_t i = 0; i < 20; i++) {
        LATAbits.LATA0 ^= 1;
        toggles++;
        delay(10);
    }

    for (uint16_t i = 0; i < N; i++) {
        big[i] = (uint8_t)(i * 7u + 3u);
    }
    for (uint16_t i = 0; i < N; i++) {
        by_index = (uint16_t)(by_index + big[i]);
    }
    for (const uint8_t *p = big + N; p != big;) {
        by_pointer = (uint16_t)(by_pointer + *--p);
    }
    /* Constant indices are addressed directly, with MOVLB and an offset. */
    big[N - 1] ^= 0xFF;
    big[N / 2] += 1;

    if (toggles != 20) code |= 1;
    if (LATA & 1) code |= 2;
    if (by_index != by_pointer) code |= 4;
    if (big[0] != 3) code |= 8;
    if (big[N - 1] != (uint8_t)~(uint8_t)((N - 1) * 7u + 3u)) code |= 16;
    if (big[N / 2] != (uint8_t)((N / 2) * 7u + 4u)) code |= 32;
    if (marker != 0xBEEF) code |= 64;

    EXIT_LAT = code;
    for (;;) {
        SLEEP();
    }
}
