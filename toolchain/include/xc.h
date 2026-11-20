/* Device support for the PIC16F1 LLVM toolchain. Select the device with
 * -mcpu=<name>; this header then declares its registers. */
#ifndef __XC_H
#define __XC_H

#if defined(__PIC16F1032__)
#include <pic16f1032.h>
#elif defined(__PIC16F1064__)
#include <pic16f1064.h>
#elif defined(__PIC16F15276__)
#include <pic16f15276.h>
#elif defined(__PIC16F1619__)
#include <pic16f1619.h>
#elif defined(__PIC16F1829__)
#include <pic16f1829.h>
#elif defined(__PIC16F18877__)
#include <pic16f18877.h>
#elif defined(__PIC16F1937__)
#include <pic16f1937.h>
#else
#error "no device header for this -mcpu value"
#endif

#define NOP() __asm__ volatile("nop")
#define SLEEP() __asm__ volatile("sleep")
#define CLRWDT() __asm__ volatile("clrwdt")
#define RESET() __asm__ volatile("reset")

/* Delays, as in XC8. _delay(n) takes at least n instruction cycles and up
 * to a dozen more (and never fewer than 16); __delay_ms and __delay_us need
 * _XTAL_FREQ, the oscillator frequency in Hz, defined first. The argument
 * must be a constant for the arithmetic to be done by the compiler.
 * Interrupts lengthen a delay. */
void __delay_loops(unsigned long loops);
#define _delay(cycles)                                                         \
    __delay_loops((unsigned long)(cycles) > 16UL                               \
                      ? ((unsigned long)(cycles) - 7UL) / 10UL : 0UL)
#define __delay_ms(ms) _delay((unsigned long)((ms) * (_XTAL_FREQ / 4000.0)))
#define __delay_us(us) _delay((unsigned long)((us) * (_XTAL_FREQ / 4000000.0)))

/* XC8's names for things this compiler spells otherwise. */
#define __interrupt(...) __attribute__((interrupt))
#define __persistent __attribute__((section(".noinit")))
#define __section(name) __attribute__((section(name)))
#define __pack __attribute__((packed))
#define __reentrant
#define __near
#define __far
typedef _Bool __bit;
#define __nop() NOP()
#define ei() (INTCONbits.GIE = 1)
#define di() (INTCONbits.GIE = 0)

/* Eight bytes of initial data EEPROM content. Each use follows the one
 * before, from the start of the EEPROM; one use to a line. */
#define __EEPROM_CAT2(a, b) a##b
#define __EEPROM_CAT(a, b) __EEPROM_CAT2(a, b)
#define __EEPROM_DATA(a, b, c, d, e, f, g, h)                                  \
    __attribute__((section(".eeprom"), used))                                  \
    static const unsigned int __EEPROM_CAT(__eeprom_data_, __LINE__)[8] = {    \
        (unsigned char)(a), (unsigned char)(b), (unsigned char)(c),            \
        (unsigned char)(d), (unsigned char)(e), (unsigned char)(f),            \
        (unsigned char)(g), (unsigned char)(h)}

/* The configuration words can be given as XC8 gives them, with
 *     #pragma config FOSC = INTOSC, WDTE = OFF
 * (the linker makes the words; a setting not given keeps its erased
 * value), or whole, with this. Use one or the other, once per program. */
#define PIC_CONFIG_WORDS(word1, word2)                                         \
    __attribute__((section(".config"), used))                                  \
    static const unsigned int __pic_config_words[2] = {(word1), (word2)}

#endif
