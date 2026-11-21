// The Arduino language for PIC16F1 microcontrollers.
//
// A pin is a number: 8 * port + bit, so RA0 is 0, RB3 is 11, RE2 is 34
// (PIN_RA0 and so on name them; A0 to A13 are the pins of the analog
// channels). The registers of the ports stand in the same order in the
// part, so no table is needed to find one from a pin number, and when the
// pin is a constant, as it nearly always is, digitalWrite, digitalRead and
// pinMode compile to the one or two bit instructions they stand for.
//
// See docs/arduino.md for what is here and what is not.
#ifndef Arduino_h
#define Arduino_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef F_CPU
#define F_CPU 16000000UL
#endif
// (For the delay macros of <xc.h>.)
#ifndef _XTAL_FREQ
#define _XTAL_FREQ F_CPU
#endif
#include <xc.h>

#ifndef ARDUINO
#define ARDUINO 10800
#endif
#define ARDUINO_ARCH_PIC16F1 1

#ifdef __cplusplus
extern "C" {
#endif

// ---- Constants --------------------------------------------------------------
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define LSBFIRST 0
#define MSBFIRST 1
#define CHANGE 1
#define FALLING 2
#define RISING 3
#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

#define PI 3.1415926535897932384626433832795
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105

// analogReference
#define DEFAULT 0        // VDD
#define EXTERNAL 2       // the VREF+ pin
#define INTERNAL1V024 0x81
#define INTERNAL2V048 0x82
#define INTERNAL4V096 0x83
#define INTERNAL INTERNAL1V024

typedef bool boolean;
typedef uint8_t byte;
typedef unsigned int word;

// ---- Small things -------------------------------------------------------------
#ifndef __cplusplus
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define abs(x) ((x) > 0 ? (x) : -(x))
#endif
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#define round(x) ((x) >= 0 ? (long)((x) + 0.5) : (long)((x) - 0.5))
#define radians(deg) ((deg) * DEG_TO_RAD)
#define degrees(rad) ((rad) * RAD_TO_DEG)
#define sq(x) ((x) * (x))

#define lowByte(w) ((uint8_t)((w) & 0xff))
#define highByte(w) ((uint8_t)((w) >> 8))
#define bit(b) (1UL << (b))
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitToggle(value, bit) ((value) ^= (1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))

#define interrupts() (INTCONbits.GIE = 1)
#define noInterrupts() (INTCONbits.GIE = 0)

#define clockCyclesPerMicrosecond() (F_CPU / 1000000L)

// Constant data is in flash here without being asked.
#define PROGMEM
#define PSTR(s) (s)
#define F(s) (s)
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define pgm_read_word(p) (*(const uint16_t *)(p))
#define pgm_read_dword(p) (*(const uint32_t *)(p))
#define pgm_read_ptr(p) (*(void *const *)(p))

// ---- Pins ------------------------------------------------------------------------
#define digitalPinToPort(pin) ((uint8_t)(pin) >> 3)
#define digitalPinToBitMask(pin) ((uint8_t)(1u << ((pin) & 7)))
#define portOutputRegister(port) (&LATA + (port))
#define portInputRegister(port) (&PORTA + (port))
#define portModeRegister(port) (&TRISA + (port))
#define NOT_A_PIN 0xFF
#define NOT_AN_INTERRUPT 0xFF

#include "pins_arduino.h"

void _pinMode(uint8_t pin, uint8_t mode);
void _digitalWrite(uint8_t pin, uint8_t value);
uint8_t _digitalRead(uint8_t pin);

#define _ARDUINO_INLINE static inline __attribute__((always_inline))

_ARDUINO_INLINE void pinMode(uint8_t pin, uint8_t mode)
{
    if (__builtin_constant_p(pin) && __builtin_constant_p(mode)) {
        // (Port C has no analog inputs, and so no such register.)
        if (digitalPinToPort(pin) != 2)
            *(&ANSELA + digitalPinToPort(pin)) &= (uint8_t)~digitalPinToBitMask(pin);
        if (mode == OUTPUT) {
            *portModeRegister(digitalPinToPort(pin)) &= (uint8_t)~digitalPinToBitMask(pin);
        } else {
            *portModeRegister(digitalPinToPort(pin)) |= digitalPinToBitMask(pin);
            // Only port B has pull-ups.
            if (digitalPinToPort(pin) == 1) {
                if (mode == INPUT_PULLUP)
                    WPUB |= digitalPinToBitMask(pin);
                else
                    WPUB &= (uint8_t)~digitalPinToBitMask(pin);
            }
        }
    } else {
        _pinMode(pin, mode);
    }
}

_ARDUINO_INLINE void digitalWrite(uint8_t pin, uint8_t value)
{
    if (__builtin_constant_p(pin)) {
        if (value)
            *portOutputRegister(digitalPinToPort(pin)) |= digitalPinToBitMask(pin);
        else
            *portOutputRegister(digitalPinToPort(pin)) &= (uint8_t)~digitalPinToBitMask(pin);
    } else {
        _digitalWrite(pin, value);
    }
}

_ARDUINO_INLINE int digitalRead(uint8_t pin)
{
    if (__builtin_constant_p(pin))
        return (*portInputRegister(digitalPinToPort(pin)) & digitalPinToBitMask(pin)) != 0;
    return _digitalRead(pin);
}

// Not in the Arduino language, but one instruction where the others are
// a read and a write.
_ARDUINO_INLINE void digitalToggle(uint8_t pin)
{
    *portOutputRegister(digitalPinToPort(pin)) ^= digitalPinToBitMask(pin);
}

int analogRead(uint8_t pin);
void analogReference(uint8_t mode);
void analogWrite(uint8_t pin, int value);

// ---- Time ---------------------------------------------------------------------------
unsigned long millis(void);
unsigned long micros(void);
void _delayShort(unsigned int ms);
void _delayLong(unsigned long ms);

// Counted on timer 0, so that interrupts do not lengthen it. (A constant
// delay of up to a minute leaves no arithmetic on 32 bits in the program.)
_ARDUINO_INLINE void delay(unsigned long ms)
{
    if (__builtin_constant_p(ms) && ms <= 65535)
        _delayShort((unsigned int)ms);
    else
        _delayLong(ms);
}
void _delayMicroseconds(unsigned int us);
_ARDUINO_INLINE void delayMicroseconds(unsigned int us)
{
    if (__builtin_constant_p(us))
        __delay_us(us);
    else
        _delayMicroseconds(us);
}
#define yield()

unsigned long pulseIn(uint8_t pin, uint8_t state, unsigned long timeout);
void shiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder, uint8_t value);
uint8_t shiftIn(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder);

// ---- Interrupts on pins ------------------------------------------------------------------
// Every pin of port B can interrupt, on either edge or both.
#define digitalPinToInterrupt(pin) (digitalPinToPort(pin) == 1 ? ((pin) & 7) : NOT_AN_INTERRUPT)
void attachInterrupt(uint8_t interrupt, void (*handler)(void), uint8_t mode);
void detachInterrupt(uint8_t interrupt);

void init(void);
void setup(void);
void loop(void);

#ifdef __cplusplus
} // extern "C"

// ---- C++ ------------------------------------------------------------------------------------
template <typename T, typename U> constexpr auto min(T a, U b) -> decltype(a + b) { return a < b ? a : b; }
template <typename T, typename U> constexpr auto max(T a, U b) -> decltype(a + b) { return a > b ? a : b; }
#undef abs
template <typename T> constexpr T abs(T x) { return x > 0 ? x : -x; }

long random(long max);
long random(long min, long max);
void randomSeed(unsigned long seed);
long map(long value, long fromLow, long fromHigh, long toLow, long toHigh);
unsigned int makeWord(uint8_t high, uint8_t low);
#define word(...) makeWord(__VA_ARGS__)

inline bool isDigit(char c) { return c >= '0' && c <= '9'; }
inline bool isAlpha(char c) { return (c | 0x20) >= 'a' && (c | 0x20) <= 'z'; }
inline bool isAlphaNumeric(char c) { return isDigit(c) || isAlpha(c); }
inline bool isSpace(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
inline bool isWhitespace(char c) { return c == ' ' || c == '\t'; }
inline bool isUpperCase(char c) { return c >= 'A' && c <= 'Z'; }
inline bool isLowerCase(char c) { return c >= 'a' && c <= 'z'; }
inline bool isHexadecimalDigit(char c) { return isDigit(c) || ((c | 0x20) >= 'a' && (c | 0x20) <= 'f'); }

#include "HardwareSerial.h"
#endif

#endif
