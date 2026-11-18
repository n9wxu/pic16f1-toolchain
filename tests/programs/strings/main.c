/* String routines written out by hand, plus decimal conversion. */
#include "bench.h"

static char buf[24];
static char num[8];

static uint8_t my_strlen(const char *s)
{
    uint8_t n = 0;
    while (s[n]) n++;
    return n;
}

static void my_strcpy(char *d, const char *s)
{
    while ((*d++ = *s++) != 0) {
    }
}

static int8_t my_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int8_t)((uint8_t)*a - (uint8_t)*b);
}

static void reverse(char *s)
{
    uint8_t i = 0, j = my_strlen(s);
    while (j > i + 1) {
        char t = s[i];
        s[i++] = s[--j];
        s[j] = t;
    }
}

static void utoa(uint16_t v, char *out)
{
    uint8_t n = 0;
    do {
        out[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    out[n] = 0;
    reverse(out);
}

void main(void)
{
    uint8_t ok = 1;
    bench_init();
    my_strcpy(buf, "enhanced mid-range");
    bench_result16("len", my_strlen(buf));
    reverse(buf);
    bench_puts(buf);
    bench_putc('\n');
    reverse(buf);
    if (my_strcmp(buf, "enhanced mid-range") != 0) ok = 0;
    if (my_strcmp("abc", "abd") >= 0) ok = 0;
    if (my_strcmp("abd", "abc") <= 0) ok = 0;
    utoa(0, num);
    bench_puts(num);
    bench_putc(' ');
    utoa(1937, num);
    bench_puts(num);
    bench_putc(' ');
    utoa(65535u, num);
    bench_puts(num);
    bench_putc('\n');
    if (my_strcmp(num, "65535") != 0) ok = 0;
    bench_exit(ok ? 0 : 1);
}
