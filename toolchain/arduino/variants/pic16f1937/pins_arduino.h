// Pins of the PIC16F1937 (and the other 40- and 44-pin PIC16F193x).
#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#define NUM_DIGITAL_PINS 36
#define NUM_ANALOG_INPUTS 14

// Every pin by its name on the part: 8 * port + bit.
#define _PORT_PINS(L, base)                                                    \
    PIN_R##L##0 = base, PIN_R##L##1, PIN_R##L##2, PIN_R##L##3, PIN_R##L##4,    \
    PIN_R##L##5, PIN_R##L##6, PIN_R##L##7
enum { _PORT_PINS(A, 0), _PORT_PINS(B, 8), _PORT_PINS(C, 16), _PORT_PINS(D, 24), _PORT_PINS(E, 32) };
#undef _PORT_PINS

// The pins of the analog channels AN0 to AN13.
#define A0 PIN_RA0
#define A1 PIN_RA1
#define A2 PIN_RA2
#define A3 PIN_RA3
#define A4 PIN_RA5
#define A5 PIN_RE0
#define A6 PIN_RE1
#define A7 PIN_RE2
#define A8 PIN_RB2
#define A9 PIN_RB3
#define A10 PIN_RB1
#define A11 PIN_RB4
#define A12 PIN_RB0
#define A13 PIN_RB5
// What analogRead takes besides a pin: the temperature indicator, the
// D/A converter's output and the 1.024 V reference.
#define A_TEMPERATURE (0x80 | 29)
#define A_DAC (0x80 | 30)
#define A_FVR (0x80 | 31)

// The analog channel of a pin, or -1. (A switch the compiler folds away
// when the pin is a constant.)
static inline int8_t analogPinToChannel(uint8_t pin)
{
    switch (pin) {
    case PIN_RA0: return 0;
    case PIN_RA1: return 1;
    case PIN_RA2: return 2;
    case PIN_RA3: return 3;
    case PIN_RA5: return 4;
    case PIN_RE0: return 5;
    case PIN_RE1: return 6;
    case PIN_RE2: return 7;
    case PIN_RB2: return 8;
    case PIN_RB3: return 9;
    case PIN_RB1: return 10;
    case PIN_RB4: return 11;
    case PIN_RB0: return 12;
    case PIN_RB5: return 13;
    }
    return -1;
}

// The pins analogWrite gives a PWM signal on (the five CCP modules).
#define PIN_PWM1 PIN_RC2
#define PIN_PWM2 PIN_RC1
#define PIN_PWM3 PIN_RE0
#define PIN_PWM4 PIN_RD1
#define PIN_PWM5 PIN_RE2
#define digitalPinHasPWM(p)                                                    \
    ((p) == PIN_PWM1 || (p) == PIN_PWM2 || (p) == PIN_PWM3 || (p) == PIN_PWM4 || (p) == PIN_PWM5)

// The serial port, I2C and SPI.
#define PIN_SERIAL_TX PIN_RC6
#define PIN_SERIAL_RX PIN_RC7
#define SDA PIN_RC4
#define SCL PIN_RC3
#define SCK PIN_RC3
#define MISO PIN_RC4
#define MOSI PIN_RC5
#define SS PIN_RA5

// The F1 Evaluation Platform: its four LEDs (D1, D2 and D4 are on PWM
// pins), its button (low when pressed) and its potentiometer.
#define LED_BUILTIN PIN_RD1
#define LED_D1 PIN_RD1
#define LED_D2 PIN_RE2
#define LED_D3 PIN_RE1
#define LED_D4 PIN_RE0
#define BUTTON_S1 PIN_RD2
#define POT A8

#endif
