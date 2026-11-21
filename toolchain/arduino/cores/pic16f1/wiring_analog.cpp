// analogRead (the 10-bit converter) and analogWrite (PWM from the five
// CCP modules on timer 2).
#include "Arduino.h"

// ADCON1 without the clock: right-justified, and the positive reference.
static uint8_t reference = 0x80;

// The converter's clock: the oscillator divided to 2 us a bit where that
// can be done, the converter's own RC clock above 8 MHz.
#if F_CPU > 8000000UL
#define ADC_CLOCK 0x70
#elif F_CPU == 8000000UL
#define ADC_CLOCK 0x50
#elif F_CPU == 4000000UL
#define ADC_CLOCK 0x10
#elif F_CPU == 2000000UL
#define ADC_CLOCK 0x40
#else
#define ADC_CLOCK 0x00
#endif

static void reference_on(uint8_t gain)
{
    FVRCON = (uint8_t)((FVRCON & 0x7C) | 0x80 | gain);
    while (!FVRCONbits.FVRRDY) {
    }
}

extern "C" {
void analogReference(uint8_t mode)
{
    if (mode & 0x80) {
        reference_on(mode & 3);
        reference = 0x83;
    } else {
        reference = 0x80 | mode;
    }
}

int analogRead(uint8_t pin)
{
    uint8_t channel;
    if (pin & 0x80) {
        channel = pin & 0x1F;
        // The reference as an input: 1.024 V, if nothing else has set it.
        if (channel == 31 && !(FVRCON & 3))
            reference_on(1);
        if (channel == 29)
            FVRCON |= 0x20;
    } else {
        int8_t found = analogPinToChannel(pin);
        if (found < 0)
            return 0;
        channel = (uint8_t)found;
        uint8_t mask = digitalPinToBitMask(pin);
        *portModeRegister(digitalPinToPort(pin)) |= mask;
        *(&ANSELA + digitalPinToPort(pin)) |= mask;
    }
    ADCON1 = reference | ADC_CLOCK;
    ADCON0 = (uint8_t)(channel << 2 | 1);
    // The sampling capacitor needs about 5 us to follow the new input.
    __delay_us(6);
    // A conversion takes at most 200 instruction cycles. One that has not
    // finished in several times that never will: the PIC16F193x can lose
    // one (errata DS80000479). It is stopped and started again.
    for (;;) {
        ADCON0bits.GO_nDONE = 1;
        for (uint8_t patience = 255; patience; patience--)
            if (!ADCON0bits.GO_nDONE)
                return (int)ADRES;
        ADCON0bits.GO_nDONE = 0;
    }
}

// 0 is always low and 255 always high; timer 2 wraps about a thousand
// times a second at 16 MHz. On a pin without PWM, low below 128 and high
// from there.
void analogWrite(uint8_t pin, int value)
{
    if (!digitalPinHasPWM(pin)) {
        _pinMode(pin, OUTPUT);
        _digitalWrite(pin, value >= 128);
        return;
    }
    if (!T2CONbits.TMR2ON) {
        PR2 = 254;
#if F_CPU >= 16000000UL
        T2CON = 0x06; // on, divided by 16
#elif F_CPU >= 4000000UL
        T2CON = 0x05; // by 4
#else
        T2CON = 0x04;
#endif
    }
    uint8_t duty = (uint8_t)value;
    switch (pin) {
    case PIN_PWM1:
        CCPR1L = duty;
        CCP1CON = 0x0C;
        break;
    case PIN_PWM2:
        CCPR2L = duty;
        CCP2CON = 0x0C;
        break;
    case PIN_PWM3:
        CCPR3L = duty;
        CCP3CON = 0x0C;
        break;
    case PIN_PWM4:
        CCPR4L = duty;
        CCP4CON = 0x0C;
        break;
    default:
        CCPR5L = duty;
        CCP5CON = 0x0C;
        break;
    }
    _pinMode(pin, OUTPUT);
}
}
