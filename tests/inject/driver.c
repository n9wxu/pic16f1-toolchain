#include "bench.h"
#include "iface.h"
static void put(uint8_t c) { bench_putc((char)c); }
static uint8_t upper(uint8_t c) { uint8_t tmp[2]; tmp[0] = c; tmp[1] = 32; return (c >= 'a' && c <= 'z') ? (uint8_t)(tmp[0] - tmp[1]) : c; }
const struct io_ops upper_ops = { put, upper };
