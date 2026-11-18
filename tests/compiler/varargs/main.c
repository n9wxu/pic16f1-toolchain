/* Variadic functions. */
#include "bench.h"
#include <stdarg.h>
#define NOINLINE __attribute__((noinline))

NOINLINE static uint16_t sum_ints(int count, ...)
{
    va_list ap;
    uint16_t s = 0;
    va_start(ap, count);
    while (count--) s = (uint16_t)(s + (uint16_t)va_arg(ap, int));
    va_end(ap);
    return s;
}

NOINLINE static uint32_t mixed(const char *fmt, ...)
{
    va_list ap;
    uint32_t acc = 0;
    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        switch (*fmt) {
        case 'i': acc += (uint32_t)(uint16_t)va_arg(ap, int); break;
        case 'l': acc ^= va_arg(ap, uint32_t); break;
        case 's': { const char *s = va_arg(ap, const char *); while (*s) acc += (uint8_t)*s++; break; }
        case 'p': acc += *va_arg(ap, uint8_t *); break;
        default: break;
        }
        acc = (acc << 1) | (acc >> 31);
    }
    va_end(ap);
    return acc;
}

NOINLINE static uint16_t forward(int n, va_list ap)
{
    uint16_t s = 0;
    while (n--) s = (uint16_t)(s * 3u + (uint16_t)va_arg(ap, int));
    return s;
}

NOINLINE static uint16_t wrapper(int n, ...)
{
    va_list ap;
    uint16_t r;
    va_start(ap, n);
    r = forward(n, ap);
    va_end(ap);
    return r;
}

void main(void)
{
    uint8_t byte = 0x5A;
    uint16_t a, c;
    uint32_t b;
    bench_init();
    a = sum_ints(5, 1, 20, 300, 4000, -7);
    b = mixed("ilsp", 1234, (uint32_t)0xDEADBEEFUL, "pic", &byte);
    c = wrapper(4, 1, 2, 3, 4);
    bench_result16("sum", a);
    bench_result32("mixed", b);
    bench_result16("fwd", c);
    bench_result16("none", sum_ints(0));
    bench_exit(a == 4314 && c == 58 ? 0 : 1);
}
