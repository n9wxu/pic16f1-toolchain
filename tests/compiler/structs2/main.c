/* Structs passed and returned by value, unions, bit-fields, nested data. */
#include "bench.h"
#define NOINLINE __attribute__((noinline))

struct small { uint8_t a; uint16_t b; };
struct big { uint16_t w[4]; uint32_t d; uint8_t tag; };
struct bits { unsigned a : 3; unsigned b : 5; unsigned c : 1; unsigned d : 7; };
union view { uint32_t whole; uint8_t bytes[4]; struct { uint16_t lo, hi; } half; };
struct outer { struct small s[3]; struct big *link; uint8_t count; };

static volatile uint8_t one = 1;

NOINLINE static struct small make_small(uint8_t a, uint16_t b)
{
    struct small s;
    s.a = a;
    s.b = b;
    return s;
}

NOINLINE static struct big make_big(uint16_t base)
{
    struct big b;
    for (uint8_t i = 0; i < 4; i++) b.w[i] = (uint16_t)(base + i * 0x111u);
    b.d = (uint32_t)base * 65537UL;
    b.tag = (uint8_t)base;
    return b;
}

NOINLINE static uint16_t use_small(struct small s) { s.a++; return (uint16_t)(s.a + s.b); }

NOINLINE static uint32_t use_big(struct big b)
{
    uint32_t r = b.d;
    for (uint8_t i = 0; i < 4; i++) r += b.w[i];
    b.w[0] = 0;
    return r + b.tag;
}

NOINLINE static struct big twist(struct big b, struct small s)
{
    b.w[s.a & 3] ^= s.b;
    b.d -= s.b;
    return b;
}

void main(void)
{
    struct small s = make_small((uint8_t)(4 + one), 1000);
    struct big b = make_big((uint16_t)(0x1200 + one));
    struct big c;
    struct bits f;
    union view v;
    struct outer o;
    uint16_t r1;
    uint32_t r2, r3;

    bench_init();
    r1 = use_small(s);
    r2 = use_big(b);
    c = twist(b, s);
    r3 = use_big(c);
    bench_result16("small", r1);
    bench_result16("kept", (uint16_t)(s.a + b.w[0]));
    bench_result32("big", r2);
    bench_result32("twist", r3);

    f.a = (unsigned)(5 + one);
    f.b = 0x15;
    f.c = one;
    f.d = 0x55;
    f.b++;
    f.a ^= 3;
    bench_result16("bits", (uint16_t)(f.a | (f.b << 3) | (f.c << 8) | (f.d << 9)));

    v.whole = 0x11223344UL + one;
    v.bytes[2] = 0xAB;
    v.half.lo++;
    bench_result32("union", v.whole);

    for (uint8_t i = 0; i < 3; i++) o.s[i] = make_small(i, (uint16_t)(i * 500u + one));
    o.link = &c;
    o.count = 3;
    o.s[1] = o.s[2];
    o.link->tag = (uint8_t)(o.s[1].b >> 2);
    bench_result16("outer", (uint16_t)(o.s[0].b + o.s[1].b + o.s[2].b + c.tag + o.count));
    bench_exit(r1 == 1006 && s.a == 5 ? 0 : 1);
}
