/* Single-precision floating point for the PIC16F1: add, subtract, multiply,
 * divide, compare, and conversion to and from 32-bit integers.
 *
 * IEEE 754 binary32, round to nearest even, with subnormals, infinities and
 * NaNs; no exception flags and no other rounding mode. Results are the same,
 * bit for bit, as the generic routines of LLVM's compiler-rt that these
 * replace (an invalid operation gives the NaN 0x7FC00000; a NaN operand
 * comes back quiet, the first one if both are), which tests/host/float.c
 * checks against a host's own arithmetic.
 *
 * Those routines are written for 32- and 64-bit machines: they multiply
 * 32 x 32 to 64 bits and divide by Newton's method, and each is 1,500 to
 * 2,000 words here. These are written for an 8-bit one: the significand is
 * 24 bits in a 32-bit word, a product is made by shifting and adding and a
 * quotient by shifting and subtracting, and one routine rounds and packs
 * every result.
 *
 * Compiled for a host with -DFLOAT_HOST_TEST the functions take and return
 * the bits of a float and are named pic_...; see tests/host/float.c.
 */
#ifdef FLOAT_HOST_TEST
#include <stdint.h>
typedef uint32_t bits;
typedef int32_t sbits;
typedef uint8_t byte;
typedef int16_t exponent;
typedef bits real;
#define NAME(n) pic_##n
#define BITS(x) (x)
#define REAL(x) (x)
#else
typedef unsigned long bits;     /* 32 bits */
typedef long sbits;
typedef unsigned char byte;
typedef int exponent;           /* 16 bits */
typedef float real;
#define NAME(n) __##n
typedef union { float f; bits b; } pun;
#define BITS(x) (((pun){.f = (x)}).b)
#define REAL(x) (((pun){.b = (x)}).f)
#endif

#define SIGN 0x80000000UL
#define INF 0x7F800000UL
#define QUIET 0x00400000UL
#define DEFAULT_NAN 0x7FC00000UL
#define HIDDEN 0x00800000UL     /* the leading 1 of a significand */
/* A significand at work has its leading 1 at bit 26 and three more bits
 * below the last place: guard, round and sticky. */
#define TOP (HIDDEN << 3)

#define IS_NAN(x) (((x) & ~SIGN) > INF)

/* Shift right, keeping in the lowest bit whether anything was lost. */
static bits shift_sticky(bits m, byte n)
{
    if (n > 31)
        return m != 0;
    while (n--)
        m = (m >> 1) | (m & 1);
    return m;
}

/* Round and pack: the value is m / 2^26 * 2^(e - 127), m with its leading
 * 1 at bit 26 unless e is 1 or less; s is the sign. */
static bits pack(bits s, exponent e, bits m)
{
    byte low;
    if (e < 1) {
        /* Too small for a normal number: a subnormal, or zero. */
        m = shift_sticky(m, e < -30 ? 32 : (byte)(1 - e));
        e = 1;
    }
    low = (byte)m & 7;
    m >>= 3;
    if (low > 4 || (low == 4 && (m & 1)))
        m++;
    /* A carry out of the rounding lands in the exponent by itself: m is
     * then 2^24, which is the next exponent with a zero fraction. */
    if (m >= HIDDEN && e >= 254 + (m < 2 * HIDDEN))
        return s | INF;
    return s | (((bits)(e - 1) << 23) + m);
}

/* The exponent and significand of a number that is not zero, infinite or
 * a NaN, the significand with its leading 1 at bit 23: a subnormal is
 * shifted up to that. */
static exponent unpack(bits x, bits *m)
{
    exponent e = (exponent)((x >> 23) & 0xFF);
    bits f = x & (HIDDEN - 1);
    if (e)
        f |= HIDDEN;
    else
        for (e = 1; !(f & HIDDEN); e--)
            f <<= 1;
    *m = f;
    return e;
}

static bits add(bits a, bits b)
{
    bits ma, mb, s;
    exponent ea, eb;
    if (IS_NAN(a))
        return a | QUIET;
    if (IS_NAN(b))
        return b | QUIET;
    /* The larger in magnitude first: it gives the sign and the exponent. */
    if ((a & ~SIGN) < (b & ~SIGN)) {
        bits t = a;
        a = b;
        b = t;
    }
    if ((a & ~SIGN) == INF) {
        /* Infinity: minus an equal infinity is invalid. */
        if ((b & ~SIGN) == INF && ((a ^ b) & SIGN))
            return DEFAULT_NAN;
        return a;
    }
    s = a & SIGN;
    ea = (exponent)((a >> 23) & 0xFF);
    eb = (exponent)((b >> 23) & 0xFF);
    ma = a & (HIDDEN - 1);
    mb = b & (HIDDEN - 1);
    if (ea)
        ma |= HIDDEN;
    else
        ea = 1;
    if (eb)
        mb |= HIDDEN;
    else
        eb = 1;
    ma <<= 3;
    mb = shift_sticky(mb << 3, ea - eb > 31 ? 32 : (byte)(ea - eb));
    if ((a ^ b) & SIGN) {
        ma -= mb;
        if (ma == 0)
            return 0;           /* an exact zero is +0 */
        while (!(ma & TOP) && ea > 1) {
            ma <<= 1;
            ea--;
        }
    } else {
        ma += mb;
        if (ma & (TOP << 1)) {
            ma = (ma >> 1) | (ma & 1);
            ea++;
        }
    }
    return pack(s, ea, ma);
}

real NAME(addsf3)(real x, real y)
{
    return REAL(add(BITS(x), BITS(y)));
}

real NAME(subsf3)(real x, real y)
{
    /* (As compiler-rt does it: a NaN subtracted comes back with its sign
     * changed.) */
    return REAL(add(BITS(x), BITS(y) ^ SIGN));
}

/* What multiplication and division do with a NaN, an infinity or a zero;
 * `done` says whether that settled it. For division, `divide` is set. */
static bits special(bits a, bits b, byte divide, byte *done)
{
    bits s = (a ^ b) & SIGN;
    bits aa = a & ~SIGN, ab = b & ~SIGN;
    *done = 1;
    if (aa > INF)
        return a | QUIET;
    if (ab > INF)
        return b | QUIET;
    if (aa == INF) {
        /* Infinity times zero, and infinity over infinity, are invalid. */
        if (divide ? ab == INF : ab == 0)
            return DEFAULT_NAN;
        return s | INF;
    }
    if (ab == INF) {
        if (divide)
            return s;
        return aa == 0 ? DEFAULT_NAN : s | INF;
    }
    if (aa == 0) {
        if (divide && ab == 0)
            return DEFAULT_NAN;
        return s;
    }
    if (ab == 0)
        return divide ? s | INF : s;
    *done = 0;
    return s;
}

real NAME(mulsf3)(real x, real y)
{
    bits a = BITS(x), b = BITS(y), ma, mb, hi = 0, lo = 0, s;
    exponent e;
    byte i, done;
    s = special(a, b, 0, &done);
    if (done)
        return REAL(s);
    e = unpack(a, &ma);
    e += unpack(b, &mb) - 127;
    /* The 48-bit product, in hi and the top 24 bits of lo. */
    for (i = 24; i; i--) {
        if (mb & 1)
            hi += ma;
        lo = (lo >> 1) | (hi << 31);
        hi >>= 1;
        mb >>= 1;
    }
    /* It is in [2^46, 2^48): its leading 1 is bit 22 or 23 of hi. */
    if (hi & HIDDEN) {
        e++;
        lo = (lo >> 1) | (lo & 1);
    } else {
        hi <<= 1;
        if (lo & SIGN)
            hi |= 1;
        lo &= ~SIGN;
    }
    hi = (hi << 3) | (lo >> 28) | ((lo & 0x0FFFFFFFUL) != 0);
    return REAL(pack(s, e, hi));
}

real NAME(divsf3)(real x, real y)
{
    bits a = BITS(x), b = BITS(y), ma, mb, q = 0, s;
    exponent e;
    byte i, done;
    s = special(a, b, 1, &done);
    if (done)
        return REAL(s);
    e = unpack(a, &ma);
    e -= unpack(b, &mb) - 127;
    /* A quotient in [1, 2). */
    if (ma < mb) {
        ma <<= 1;
        e--;
    }
    /* 26 bits of it: the 24 and two more; what is left over is sticky. */
    for (i = 26; i; i--) {
        q <<= 1;
        if (ma >= mb) {
            ma -= mb;
            q |= 1;
        }
        ma <<= 1;
    }
    q = (q << 1) | (ma != 0);
    return REAL(pack(s, e, q));
}

/* -1, 0 or 1 as a is less than, equal to or greater than b; 2 if either
 * is a NaN. */
static signed char compare(bits a, bits b)
{
    if (IS_NAN(a) || IS_NAN(b))
        return 2;
    if (((a | b) & ~SIGN) == 0)
        return 0;               /* +0 and -0 are equal */
    if ((a ^ b) & SIGN)
        return a & SIGN ? -1 : 1;
    if (a == b)
        return 0;
    return (a < b) != ((a & SIGN) != 0) ? -1 : 1;
}

/* The compiler tests the result against zero the way the name says, and
 * an unordered pair must then come out false: 1 for these ... (The result
 * is a long, as in compiler-rt and libgcc: the compiler's two instruction
 * selectors expect 32 and 16 bits of it.) */
#ifdef FLOAT_HOST_TEST
int pic_cmpsf2(real x, real y) { return compare(x, y); }
#else
long __lesf2(float x, float y)
{
    signed char c = compare(BITS(x), BITS(y));
    return c == 2 ? 1 : c;
}
long __eqsf2(float x, float y) __attribute__((alias("__lesf2")));
long __nesf2(float x, float y) __attribute__((alias("__lesf2")));
long __ltsf2(float x, float y) __attribute__((alias("__lesf2")));
long __cmpsf2(float x, float y) __attribute__((alias("__lesf2")));

/* ... and -1 for these. */
long __gesf2(float x, float y)
{
    signed char c = compare(BITS(x), BITS(y));
    return c == 2 ? -1 : c;
}
long __gtsf2(float x, float y) __attribute__((alias("__gesf2")));

long __unordsf2(float x, float y)
{
    return compare(BITS(x), BITS(y)) == 2;
}
#endif

/* The magnitude of x as an integer, toward zero: 0 below 1, and all ones
 * from 2^32 up, which a NaN counts as too (as in compiler-rt: the
 * conversions then give the largest value of the sign). */
static bits magnitude(bits a)
{
    exponent e = (exponent)((a >> 23) & 0xFF);
    bits m = (a & (HIDDEN - 1)) | HIDDEN;
    if (e < 127)
        return 0;
    if (e > 158)
        return 0xFFFFFFFFUL;
    /* The value is m * 2^(e - 150). */
    if (e >= 150)
        return m << (byte)(e - 150);
    return m >> (byte)(150 - e);
}

sbits NAME(fixsfsi)(real x)
{
    bits a = BITS(x), m = magnitude(a);
    if (a & SIGN)
        return m > SIGN ? (sbits)SIGN : -(sbits)m;
    return m >= SIGN ? (sbits)(SIGN - 1) : (sbits)m;
}

bits NAME(fixunssfsi)(real x)
{
    bits a = BITS(x);
    if (a & SIGN)
        return 0;
    return magnitude(a);
}

static bits from_unsigned(bits s, bits m)
{
    exponent e = 127 + 31;
    bits low;
    if (m == 0)
        return 0;
    while (!(m & SIGN)) {
        m <<= 1;
        e--;
    }
    /* The leading 1 from bit 31 to bit 26, the five bits lost sticky. */
    low = m & 0x1F;
    m >>= 5;
    if (low)
        m |= 1;
    return pack(s, e, m);
}

real NAME(floatunsisf)(bits u)
{
    return REAL(from_unsigned(0, u));
}

real NAME(floatsisf)(sbits i)
{
    if (i < 0)
        return REAL(from_unsigned(SIGN, (bits)0 - (bits)i));
    return REAL(from_unsigned(0, (bits)i));
}
