/* Check toolchain/runtime/float.c against this machine's own IEEE single
 * arithmetic, bit for bit: scripts/test-float.sh builds this with the
 * routines compiled for the host (-DFLOAT_HOST_TEST) and runs it.
 *
 * Operands: every pair from a list of special values, and random ones of
 * several kinds (any bits; exponents close together, which is where
 * addition cancels and rounds; subnormals; values that overflow or
 * underflow when multiplied). A NaN result must be a NaN; its bits are
 * the routines' own choice. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t pic_addsf3(uint32_t, uint32_t), pic_subsf3(uint32_t, uint32_t);
uint32_t pic_mulsf3(uint32_t, uint32_t), pic_divsf3(uint32_t, uint32_t);
int pic_cmpsf2(uint32_t, uint32_t);
int32_t pic_fixsfsi(uint32_t);
uint32_t pic_fixunssfsi(uint32_t), pic_floatunsisf(uint32_t), pic_floatsisf(int32_t);

static uint32_t bits_of(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float float_of(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static int is_nan(uint32_t u) { return (u & 0x7FFFFFFF) > 0x7F800000; }

static uint64_t state = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void)
{
    state ^= state << 13; state ^= state >> 7; state ^= state << 17;
    return (uint32_t)(state >> 16);
}

static unsigned long failures, checks;

static void same(const char *what, uint32_t a, uint32_t b, uint32_t got, uint32_t want)
{
    checks++;
    if (got == want || (is_nan(got) && is_nan(want)))
        return;
    if (failures++ < 20)
        printf("%s %08x %08x: got %08x, want %08x\n", what, a, b, got, want);
}

static void pair(uint32_t a, uint32_t b)
{
    volatile float x = float_of(a), y = float_of(b);
    int want;
    same("add", a, b, pic_addsf3(a, b), bits_of(x + y));
    same("sub", a, b, pic_subsf3(a, b), bits_of(x - y));
    same("mul", a, b, pic_mulsf3(a, b), bits_of(x * y));
    same("div", a, b, pic_divsf3(a, b), bits_of(x / y));
    want = x != x || y != y ? 2 : x < y ? -1 : x > y ? 1 : 0;
    same("cmp", a, b, (uint32_t)pic_cmpsf2(a, b), (uint32_t)want);
}

static void one(uint32_t a)
{
    volatile float x = float_of(a);
    volatile uint32_t u = a;
    volatile int32_t i = (int32_t)a;
    /* To an integer: what C defines, and the saturation the routines add. */
    if (is_nan(a)) {
        same("fix", a, 0, (uint32_t)pic_fixsfsi(a), a >> 31 ? 0x80000000u : 0x7FFFFFFFu);
        same("fixuns", a, 0, pic_fixunssfsi(a), a >> 31 ? 0 : 0xFFFFFFFFu);
    } else {
        int32_t s = x >= 2147483648.0f ? INT32_MAX : x <= -2147483648.0f ? INT32_MIN : (int32_t)x;
        uint32_t n = x >= 4294967296.0f ? UINT32_MAX : x <= 0.0f ? 0 : (uint32_t)x;
        same("fix", a, 0, (uint32_t)pic_fixsfsi(a), (uint32_t)s);
        same("fixuns", a, 0, pic_fixunssfsi(a), n);
    }
    same("floatuns", a, 0, pic_floatunsisf(u), bits_of((float)u));
    same("float", a, 0, pic_floatsisf(i), bits_of((float)i));
}

int main(int argc, char **argv)
{
    static const uint32_t special[] = {
        0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x007FFFFF, 0x00800000,
        0x00800001, 0x3F800000, 0xBF800000, 0x3F7FFFFF, 0x3F800001, 0x40000000,
        0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000, 0xFF800000, 0x7FC00000, 0xFFC00000,
        0x7F800001, 0x7FA00000, 0x4F000000, 0xCF000000, 0x4F800000, 0x4EFFFFFF,
        0x4B800000, 0x4B7FFFFF, 0x3F000000, 0x3EFFFFFF, 0x00400000, 0x34000000,
        0x33800000, 0x33800001, 0x3FC00000, 0x3FBFFFFF, 0x7F000000, 0x01000000,
    };
    const unsigned n = sizeof special / sizeof *special;
    unsigned long rounds = argc > 1 ? strtoul(argv[1], 0, 0) : 2000000;
    for (unsigned i = 0; i < n; i++) {
        one(special[i]);
        for (unsigned j = 0; j < n; j++)
            pair(special[i], special[j]);
    }
    for (unsigned long k = 0; k < rounds; k++) {
        uint32_t a = rnd(), b = rnd();
        one(a);
        pair(a, b);
        /* Close exponents: b within a few binades of a. */
        b = (b & 0x807FFFFF) | ((((a >> 23) + (rnd() % 7) - 3) & 0xFF) << 23);
        pair(a, b);
        /* The same magnitude but for the last few bits: cancellation. */
        pair(a, (a ^ 0x80000000) ^ (rnd() & 7));
        /* Subnormals, with each other and with normal numbers. */
        pair(a & 0x807FFFFF, b);
        pair(a & 0x807FFFFF, rnd() & 0x807FFFFF);
        /* Products and quotients at the two ends of the range. */
        pair((a & 0x807FFFFF) | ((0x70 + (rnd() % 16)) << 23), (b & 0x807FFFFF) | ((0x7E + (rnd() % 16)) << 23));
        pair((a & 0x807FFFFF) | ((0x30 + (rnd() % 32)) << 23), (b & 0x807FFFFF) | ((0x30 + (rnd() % 80)) << 23));
        /* Integers of every size, and floats near the integer limits. */
        one(a >> (rnd() % 32));
        one((a & 0x807FFFFF) | ((0x7F + (rnd() % 36)) << 23));
    }
    printf("%lu checks, %lu failures\n", checks, failures);
    return failures != 0;
}
