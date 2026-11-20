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

/* Place the two configuration words in the image. Use once per program. */
#define PIC_CONFIG_WORDS(word1, word2)                                         \
    __attribute__((section(".config"), used))                                  \
    static const unsigned int __pic_config_words[2] = {(word1), (word2)}

#endif
