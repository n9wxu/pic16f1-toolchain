// Time: the oscillator, timer 0 counting milliseconds, and the one
// interrupt handler of the part.
#include "Arduino.h"

// Timer 0 counts instruction cycles divided by 16 (by 32 at 32 MHz) and
// interrupts as it wraps. At 16 and 32 MHz that is a count every 4 us and
// an interrupt every 1,024 us, as on the Arduinos with an AVR. A slower
// oscillator gets the same number of instructions between interrupts and
// so a coarser clock (8 us and 2 ms at 8 MHz, 16 us and 4 ms at 4 MHz, and
// so on down): the handler takes some 280 cycles, which a part running a
// million instructions a second cannot spare every millisecond.
#if F_CPU == 32000000UL
#define OSCCON_VALUE 0xF0
#elif F_CPU == 16000000UL
#define OSCCON_VALUE 0x78
#elif F_CPU == 8000000UL
#define OSCCON_VALUE 0x70
#elif F_CPU == 4000000UL
#define OSCCON_VALUE 0x68
#elif F_CPU == 2000000UL
#define OSCCON_VALUE 0x60
#elif F_CPU == 1000000UL
#define OSCCON_VALUE 0x58
#elif F_CPU == 500000UL
#define OSCCON_VALUE 0x38
#else
#error "F_CPU must be 500000, 1000000, 2000000, 4000000, 8000000, 16000000 or 32000000"
#endif
#define T0_SHIFT (F_CPU == 32000000UL ? 5 : 4)

#define MICROS_PER_TICK (4UL * (1UL << T0_SHIFT) * 1000000UL / F_CPU)
#define MICROS_PER_OVERFLOW (256UL * MICROS_PER_TICK)
#define MILLIS_INC (MICROS_PER_OVERFLOW / 1000)
// (In eighths of a millisecond, to fit a byte: 125 make one.)
#define FRACT_INC ((MICROS_PER_OVERFLOW % 1000) >> 3)
#define FRACT_MAX (1000 >> 3)

// Written by the interrupt handler only. Not volatile, so that a program
// that never asks the time carries neither the count nor the adding; the
// functions below read them as volatile.
static unsigned long timer0_millis;
static uint8_t timer0_fract;

// Parts of the core that a sketch may not use, and then does not link:
// each sets its pointer here when it is first used, and the handler calls
// the ones that are set. (In a sketch without them the compiler sees that
// nothing ever sets the pointer, and the test goes too.)
extern "C" {
void (*__arduino_serial_isr)(void);
void (*__arduino_pin_isr)(void);

void __attribute__((interrupt)) isr(void)
{
    if (INTCONbits.TMR0IF) {
        INTCONbits.TMR0IF = 0;
        timer0_millis += MILLIS_INC;
        timer0_fract += FRACT_INC;
        if (timer0_fract >= FRACT_MAX) {
            timer0_fract -= FRACT_MAX;
            timer0_millis++;
        }
    }
    if (__arduino_serial_isr)
        __arduino_serial_isr();
    if (__arduino_pin_isr)
        __arduino_pin_isr();
}

void init(void)
{
    OSCCON = OSCCON_VALUE;
    // Every pin digital, as a sketch expects; analogRead makes its own
    // pin analog.
    ANSELA = 0;
    ANSELB = 0;
    ANSELD = 0;
    ANSELE = 0;
    // Pull-ups of port B: each as pinMode says, none to begin with.
    WPUB = 0;
    OPTION_REG = T0_SHIFT - 1; // pull-ups allowed; timer 0 on the instruction clock, divided
    INTCONbits.TMR0IE = 1;
    INTCONbits.GIE = 1;
}

unsigned long millis(void)
{
    uint8_t was = INTCON;
    INTCONbits.GIE = 0;
    unsigned long now = *(volatile unsigned long *)&timer0_millis;
    if (was & 0x80)
        INTCONbits.GIE = 1;
    return now;
}

// The milliseconds and the eighths the handler keeps are the whole of the
// time up to the last wrap, exactly (every wrap is a multiple of 8 us), so
// no second count is kept for this.
unsigned long micros(void)
{
    uint8_t was = INTCON;
    INTCONbits.GIE = 0;
    unsigned long ms = *(volatile unsigned long *)&timer0_millis;
    unsigned int rest = (unsigned int)(*(volatile uint8_t *)&timer0_fract << 3);
    uint8_t count = TMR0;
    // A wrap the handler has not yet counted.
    if (INTCONbits.TMR0IF && count < 255)
        rest += (unsigned int)MICROS_PER_OVERFLOW;
    if (was & 0x80)
        INTCONbits.GIE = 1;
    return ms * 1000 + rest + count * (unsigned int)MICROS_PER_TICK;
}

// Delays count the ticks of timer 0 as they pass, in microseconds, whatever
// else (an interrupt handler, say) takes time meanwhile.
void _delayShort(unsigned int ms)
{
    uint8_t last = TMR0;
    unsigned int passed = 0;
    while (ms) {
        uint8_t now = TMR0;
        passed += (uint8_t)(now - last) * (unsigned int)MICROS_PER_TICK;
        last = now;
        if (passed >= 1000) {
            passed -= 1000;
            ms--;
        }
    }
}

void _delayLong(unsigned long ms)
{
    for (; ms > 60000; ms -= 60000)
        _delayShort(60000);
    _delayShort((unsigned int)ms);
}

// About right from 20 us up at 16 MHz; longer than asked at lower clocks,
// where the loop itself takes more than a microsecond a turn.
void _delayMicroseconds(unsigned int us)
{
#if F_CPU >= 16000000UL
    // Eight cycles a turn at 32 MHz are 1 us; at 16 MHz, 2 us.
    us >>= (F_CPU == 16000000UL);
#else
    us >>= (F_CPU == 8000000UL ? 2 : F_CPU == 4000000UL ? 3 : 4);
#endif
    while (us--) {
        __asm__ volatile("nop");
        __asm__ volatile("nop");
    }
}

unsigned long pulseIn(uint8_t pin, uint8_t state, unsigned long timeout)
{
    volatile uint8_t *port = portInputRegister(digitalPinToPort(pin));
    uint8_t mask = digitalPinToBitMask(pin);
    uint8_t want = state ? mask : 0;
    unsigned long began = micros();
    // The end of a pulse that is under way, the start of the next, its end.
    while ((*port & mask) == want)
        if (micros() - began > timeout)
            return 0;
    while ((*port & mask) != want)
        if (micros() - began > timeout)
            return 0;
    unsigned long rose = micros();
    while ((*port & mask) == want)
        if (micros() - began > timeout)
            return 0;
    return micros() - rose;
}

void shiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder, uint8_t value)
{
    for (uint8_t i = 0; i < 8; i++) {
        if (bitOrder == LSBFIRST) {
            _digitalWrite(dataPin, value & 1);
            value >>= 1;
        } else {
            _digitalWrite(dataPin, (value & 0x80) != 0);
            value <<= 1;
        }
        _digitalWrite(clockPin, HIGH);
        _digitalWrite(clockPin, LOW);
    }
}

uint8_t shiftIn(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder)
{
    uint8_t value = 0;
    for (uint8_t i = 0; i < 8; i++) {
        _digitalWrite(clockPin, HIGH);
        if (bitOrder == LSBFIRST)
            value = (uint8_t)(value >> 1 | (_digitalRead(dataPin) ? 0x80 : 0));
        else
            value = (uint8_t)(value << 1 | _digitalRead(dataPin));
        _digitalWrite(clockPin, LOW);
    }
    return value;
}
} // extern "C"
