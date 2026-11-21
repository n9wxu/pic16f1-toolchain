// Run an Arduino sketch's .hex on a simulated ATmega328P at 16 MHz and
// print what it sends on its serial port, until it sends EOT (4) or the
// cycle limit is reached. The last line on stderr is the cycles run.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim_avr.h"
#include "sim_hex.h"
#include "avr_uart.h"

static int done;
static void out(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)param;
    if (value == 4) done = 1; else putchar((int)value);
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    unsigned long long limit = argc > 2 ? strtoull(argv[2], 0, 10) : 400000000ULL;
    avr_t *avr = avr_make_mcu_by_name("atmega328p");
    if (!avr) return 3;
    avr_init(avr);
    avr->frequency = 16000000;
    avr->log = 0;
    uint32_t size = 0, base = 0;
    uint8_t *image = read_ihex_file(argv[1], &size, &base);
    if (!image) { fprintf(stderr, "cannot read %s\n", argv[1]); return 3; }
    memcpy(avr->flash + base, image, size);
    uint32_t flags = 0;
    avr_ioctl(avr, AVR_IOCTL_UART_GET_FLAGS('0'), &flags);
    flags &= ~AVR_UART_FLAG_STDIO;
    avr_ioctl(avr, AVR_IOCTL_UART_SET_FLAGS('0'), &flags);
    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_OUTPUT), out, 0);
    while (!done && avr->cycle < limit) {
        int state = avr_run(avr);
        if (state == cpu_Done || state == cpu_Crashed) break;
    }
    fflush(stdout);
    fprintf(stderr, "%llu cycles\n", (unsigned long long)avr->cycle);
    return done ? 0 : 1;
}
