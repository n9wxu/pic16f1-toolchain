/* Shared harness for the test and benchmark programs.
 *
 * Output goes out the EUSART. A program ends by calling bench_exit(), which
 * puts the exit code on PORTD and executes SLEEP. The same binary runs on the
 * simulator and on a PIC16F1937.
 */
#ifndef BENCH_H
#define BENCH_H

#include <xc.h>
#include <stdint.h>

#if defined(__XC8)
#define BENCH_ISR(name) void __interrupt() name(void)
#else
#define BENCH_ISR(name) __attribute__((interrupt)) void name(void)
#endif

void bench_init(void);
void bench_putc(char c);
void bench_puts(const char *s);
void bench_hex8(uint8_t v);
void bench_hex16(uint16_t v);
void bench_hex32(uint32_t v);
/* Print "name=XXXX\n". */
void bench_result16(const char *name, uint16_t v);
void bench_result32(const char *name, uint32_t v);
void bench_exit(uint8_t code);

#endif
