/* 64-bit integer arithmetic. */
#include "bench.h"
#define NOINLINE __attribute__((noinline))

static volatile uint8_t one = 1;

static void show(const char *name, uint64_t v)
{
    bench_puts(name);
    bench_putc('=');
    bench_hex32((uint32_t)(v >> 32));
    bench_hex32((uint32_t)v);
    bench_putc('\n');
}

NOINLINE static uint64_t mul(uint64_t a, uint64_t b) { return a * b; }
NOINLINE static uint64_t divu(uint64_t a, uint64_t b) { return a / b; }
NOINLINE static uint64_t modu(uint64_t a, uint64_t b) { return a % b; }
NOINLINE static int64_t divs(int64_t a, int64_t b) { return a / b; }
NOINLINE static uint64_t shl(uint64_t a, uint8_t n) { return a << n; }
NOINLINE static uint64_t shr(uint64_t a, uint8_t n) { return a >> n; }
NOINLINE static int64_t sar(int64_t a, uint8_t n) { return a >> n; }
NOINLINE static uint8_t lt(int64_t a, int64_t b) { return a < b; }
NOINLINE static uint8_t ult(uint64_t a, uint64_t b) { return a < b; }

void main(void)
{
    uint64_t a = 0x123456789ABCDEF0ULL + one;
    uint64_t b = 0x0FEDCBA987654321ULL;
    uint64_t f = 0;
    bench_init();
    show("add", a + b);
    show("sub", b - a);
    show("mul", mul(a, 0x1000193ULL));
    show("div", divu(a, 1000000007ULL));
    show("mod", modu(a, 1000000007ULL));
    show("sdiv", (uint64_t)divs(-(int64_t)a, 12345));
    show("shl", shl(a, (uint8_t)(12 + one)));
    show("shr", shr(a, (uint8_t)(36 + one)));
    show("sar", (uint64_t)sar(-(int64_t)b, (uint8_t)(8 + one)));
    for (uint8_t i = 0; i < 20; i++) f = f * 31 + i + a;
    show("poly", f);
    bench_result16("cmp", (uint16_t)((lt(-1, 1) << 3) | (lt(1, -1) << 2) | (ult(a, b) << 1) | ult(b, a)));
    bench_exit(lt(-1, 1) && !ult(a, b) ? 0 : 1);
}
