/* Host implementation of the bench.h interface: UART output goes to stdout
 * and bench_exit() ends the process with the exit code. */
#include "bench.h"
#include <stdio.h>
#include <stdlib.h>

host_port_t host_portb;
uint8_t host_trisb;

#undef main
extern void pic_main(void);

void bench_init(void) {}
void bench_putc(char c) { putchar(c); }
void bench_puts(const char *s) { fputs(s, stdout); }
void bench_hex8(uint8_t v) { printf("%02X", v); }
void bench_hex16(uint16_t v) { printf("%04X", v); }
void bench_hex32(uint32_t v) { printf("%08lX", (unsigned long)v); }
void bench_result16(const char *n, uint16_t v) { printf("%s=%04X\n", n, v); }
void bench_result32(const char *n, uint32_t v) { printf("%s=%08lX\n", n, (unsigned long)v); }
void bench_exit(uint8_t code) { fflush(stdout); exit(code); }

int main(void)
{
    pic_main();
    return 99;
}
