/* Switches of several shapes, each checked against a table of what it
 * should give for every value of a byte: dense (a jump table where that
 * pays), sparse, with a default in the middle of the range, with cases
 * that share code, and on a 16-bit value. For scripts/test-switch.sh. */
#include <xc.h>

__attribute__((noinline)) static unsigned char dense(unsigned char x)
{
    switch (x) {
    case 0: return 11;
    case 1: return 23;
    case 2: return 5;
    case 3: return 99;
    case 4: return 42;
    case 5: return 7;
    case 6: return 64;
    case 7: return 128;
    case 8: return 200;
    case 9: return 3;
    default: return 0;
    }
}

__attribute__((noinline)) static unsigned char offset(unsigned char x)
{
    switch (x) {
    case 100: return 1;
    case 101: return 2;
    case 102: return 3;
    case 104: return 5;
    case 105: return 6;
    case 106: return 7;
    case 107: return 8;
    default: return 0xEE;
    }
}

__attribute__((noinline)) static unsigned char sparse(unsigned char x)
{
    switch (x) {
    case 1: return 1;
    case 40: return 2;
    case 90: return 3;
    case 170: return 4;
    case 250: return 5;
    default: return 0;
    }
}

volatile unsigned char side;

__attribute__((noinline)) static void shared(unsigned char x)
{
    switch (x) {
    case 0: case 2: case 4: case 6: side += 1; break;
    case 1: case 3: side += 10; break;
    case 5: side += 100; /* and on into 7 */
    case 7: side ^= 0x55; break;
    default: break;
    }
}

__attribute__((noinline)) static unsigned char wide(unsigned int x)
{
    switch (x) {
    case 1000: return 1;
    case 1001: return 2;
    case 1002: return 3;
    case 1003: return 4;
    case 1004: return 5;
    case 1005: return 6;
    case 257: return 7;
    default: return 0;
    }
}

volatile unsigned char acc;

/* Twelve places to go, each doing something different: a table of jumps
 * at every level. */
__attribute__((noinline)) static void dispatch(unsigned char x)
{
    switch (x) {
    case 3: acc += 1; break;
    case 4: acc ^= 0x21; break;
    case 5: acc <<= 1; break;
    case 6: acc -= 7; break;
    case 7: acc |= 0x40; break;
    case 8: acc &= 0x7B; break;
    case 9: acc += acc; break;
    case 10: acc = ~acc; break;
    case 11: acc >>= 1; break;
    case 12: acc += 0x33; break;
    case 13: acc ^= 0xFF; break;
    case 14: acc -= 1; break;
    default: acc += 2; break;
    }
}

static unsigned char want_dispatch(unsigned char a, unsigned char x)
{
    if (x == 3) return a + 1;
    if (x == 4) return a ^ 0x21;
    if (x == 5) return a << 1;
    if (x == 6) return a - 7;
    if (x == 7) return a | 0x40;
    if (x == 8) return a & 0x7B;
    if (x == 9) return a + a;
    if (x == 10) return ~a;
    if (x == 11) return a >> 1;
    if (x == 12) return a + 0x33;
    if (x == 13) return a ^ 0xFF;
    if (x == 14) return a - 1;
    return a + 2;
}

static unsigned char want_dense(unsigned char x)
{
    static const unsigned char t[10] = {11, 23, 5, 99, 42, 7, 64, 128, 200, 3};
    return x < 10 ? t[x] : 0;
}

void main(void)
{
    unsigned char x = 0, bad = 0;
    unsigned int w;
    do {
        if (dense(x) != want_dense(x))
            bad |= 1;
        if (offset(x) != (x >= 100 && x <= 107 && x != 103 ? (unsigned char)(x - 99) : 0xEE))
            bad |= 2;
        if (sparse(x) != (x == 1 ? 1 : x == 40 ? 2 : x == 90 ? 3 : x == 170 ? 4 : x == 250 ? 5 : 0))
            bad |= 4;
    } while (++x);
    acc = 0x5C;
    do {
        unsigned char before = acc;
        dispatch(x);
        if (acc != want_dispatch(before, x))
            bad |= 32;
    } while (++x);
    side = 0;
    for (x = 0; x < 12; x++)
        shared(x);
    /* 4 ones, 2 tens, 100 then ^0x55 twice (5 falls into 7; then 7). */
    {
        unsigned char s = 0;
        s += 1; s += 10; s += 1; s += 10; s += 1; s += 100; s ^= 0x55; s += 1; s ^= 0x55;
        if (side != s)
            bad |= 8;
    }
    for (w = 250; w < 1100; w++) {
        unsigned char r = w == 257 ? 7 : w >= 1000 && w <= 1005 ? (unsigned char)(w - 999) : 0;
        if (wide(w) != r)
            bad |= 16;
    }
    LATD = bad ? bad : 0x5A;
    SLEEP();
}
