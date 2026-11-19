/* The <picmath.inc> macros on registers other than the compiler's: ops.S
 * places them on a block of banked RAM. Each result is compared with the
 * compiler's own arithmetic; "bad" counts the differences and "n" the
 * comparisons made. */
#include "bench.h"
#define COUNT(a) ((uint8_t)(sizeof(a) / sizeof(a)[0]))

/* a at 0, b at 4, the product or the remainder at 8, the shift count at
 * 12. After a divide a holds the quotient. */
extern uint8_t pm_regs[14];
void pm_mul8(void), pm_udivmod8(void), pm_divmod8(void);
void pm_mul16(void), pm_mul16_fsr(void), pm_udivmod16(void), pm_divmod16(void);
void pm_mul32(void), pm_udivmod32(void), pm_udivmod32_norm(void), pm_divmod32(void);
void pm_neg(void), pm_abs(void);
void pm_shl32(void), pm_lsr32(void), pm_asr32(void);
void pm_shl64(void), pm_lsr64(void), pm_asr64(void);

static const int8_t v8[] = { 0, 1, -1, 2, 7, -7, 16, 100, -100, 127, -127, -128 };
static const int16_t v16[] = { 0, 1, -1, 7, -7, 255, 256, -257, 1000, 12345, -12345,
                               32767, -32768 };
static const int32_t v32[] = { 0, 1, -1, 15, 255, -256, 65536, -65537, 123456789,
                               -123456789, 2147483647, -2147483647 - 1 };
static const uint8_t counts[] = { 0, 1, 7, 8, 9, 31, 32, 33, 63 };

static uint16_t bad, n;

static void put(uint8_t at, uint32_t v)
{
    pm_regs[at] = (uint8_t)v;
    pm_regs[at + 1] = (uint8_t)(v >> 8);
    pm_regs[at + 2] = (uint8_t)(v >> 16);
    pm_regs[at + 3] = (uint8_t)(v >> 24);
}
static uint32_t get(uint8_t at)
{
    return (uint32_t)pm_regs[at] | ((uint32_t)pm_regs[at + 1] << 8) |
           ((uint32_t)pm_regs[at + 2] << 16) | ((uint32_t)pm_regs[at + 3] << 24);
}
static void set(uint32_t a, uint32_t b)
{
    put(0, a);
    put(4, b);
}
static void check(uint32_t got, uint32_t want)
{
    n++;
    if (got != want)
        bad++;
}
#define CHECK8(at, want) check((uint8_t)get(at), (uint8_t)(want))
#define CHECK16(at, want) check((uint16_t)get(at), (uint16_t)(want))
#define CHECK32(at, want) check(get(at), (uint32_t)(want))

static void bits8(void)
{
    for (uint8_t i = 0; i < COUNT(v8); i++) {
        for (uint8_t j = 0; j < COUNT(v8); j++) {
            int8_t a = v8[i], b = v8[j];
            uint8_t ua = (uint8_t)a, ub = (uint8_t)b;
            set(ua, ub);
            pm_mul8();
            CHECK8(8, ua * ub);
            if (b == 0)
                continue;
            set(ua, ub);
            pm_udivmod8();
            CHECK8(0, ua / ub);
            CHECK8(8, ua % ub);
            if (a == -128 && b == -1)
                continue;
            set(ua, ub);
            pm_divmod8();
            CHECK8(0, a / b);
            CHECK8(8, a % b);
        }
    }
}

static void bits16(void)
{
    for (uint8_t i = 0; i < COUNT(v16); i++) {
        for (uint8_t j = 0; j < COUNT(v16); j++) {
            int16_t a = v16[i], b = v16[j];
            uint16_t ua = (uint16_t)a, ub = (uint16_t)b;
            set(ua, ub);
            pm_mul16();
            CHECK16(8, ua * ub);
            set(ua, ub);
            pm_mul16_fsr();
            CHECK16(8, ua * ub);
            if (b == 0)
                continue;
            set(ua, ub);
            pm_udivmod16();
            CHECK16(0, ua / ub);
            CHECK16(8, ua % ub);
            if (a == -32768 && b == -1)
                continue;
            set(ua, ub);
            pm_divmod16();
            CHECK16(0, a / b);
            CHECK16(8, a % b);
        }
    }
}

static void bits32(void)
{
    for (uint8_t i = 0; i < COUNT(v32); i++) {
        for (uint8_t j = 0; j < COUNT(v32); j++) {
            int32_t a = v32[i], b = v32[j];
            uint32_t ua = (uint32_t)a, ub = (uint32_t)b;
            set(ua, ub);
            pm_mul32();
            CHECK32(8, ua * ub);
            if (b == 0)
                continue;
            set(ua, ub);
            pm_udivmod32();
            CHECK32(0, ua / ub);
            CHECK32(8, ua % ub);
            set(ua, ub);
            pm_udivmod32_norm();
            CHECK32(8, ua / ub);
            CHECK32(0, ua % ub);
            if (a == -2147483647 - 1 && b == -1)
                continue;
            set(ua, ub);
            pm_divmod32();
            CHECK32(0, a / b);
            CHECK32(8, a % b);
        }
    }
}

static void signs(void)
{
    for (uint8_t i = 0; i < COUNT(v32); i++) {
        int32_t a = v32[i];
        int16_t h = v16[i];
        int8_t c = v8[i];
        set((uint32_t)a, (uint8_t)c);
        put(8, (uint16_t)h);
        pm_neg();
        CHECK32(0, 0u - (uint32_t)a);
        CHECK16(8, 0u - (uint16_t)h);
        CHECK8(4, 0u - (uint8_t)c);
        set((uint32_t)a, (uint8_t)c);
        put(8, (uint16_t)h);
        pm_abs();
        CHECK32(0, a < 0 ? 0u - (uint32_t)a : (uint32_t)a);
        CHECK16(8, h < 0 ? 0u - (uint16_t)h : (uint16_t)h);
        CHECK8(4, c < 0 ? 0u - (uint8_t)c : (uint8_t)c);
    }
}

static void shifts(void)
{
    for (uint8_t i = 0; i < COUNT(v32); i++) {
        for (uint8_t j = 0; j < COUNT(counts); j++) {
            uint8_t c = counts[j];
            int32_t a = v32[i];
            uint64_t w = ((uint64_t)(uint32_t)a << 32) | (uint32_t)v32[COUNT(v32) - 1 - i];
            uint64_t r;
            if (c < 32) {
                put(0, (uint32_t)a); pm_regs[12] = c; pm_shl32();
                CHECK32(0, (uint32_t)a << c);
                put(0, (uint32_t)a); pm_regs[12] = c; pm_lsr32();
                CHECK32(0, (uint32_t)a >> c);
                put(0, (uint32_t)a); pm_regs[12] = c; pm_asr32();
                CHECK32(0, a >> c);
            }
            set((uint32_t)w, (uint32_t)(w >> 32)); pm_regs[12] = c; pm_shl64();
            r = w << c;
            CHECK32(0, r); CHECK32(4, r >> 32);
            set((uint32_t)w, (uint32_t)(w >> 32)); pm_regs[12] = c; pm_lsr64();
            r = w >> c;
            CHECK32(0, r); CHECK32(4, r >> 32);
            set((uint32_t)w, (uint32_t)(w >> 32)); pm_regs[12] = c; pm_asr64();
            r = (uint64_t)((int64_t)w >> c);
            CHECK32(0, r); CHECK32(4, r >> 32);
        }
    }
}

void main(void)
{
    bench_init();
    bits8();
    bits16();
    bits32();
    signs();
    shifts();
    bench_result16("bad", bad);
    bench_result16("n", n);
    bench_exit(0);
}
