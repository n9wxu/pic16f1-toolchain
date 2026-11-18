/* Dense and sparse switch statements, fallthrough and nesting. */
#include "bench.h"
#define NOINLINE __attribute__((noinline))

enum op { OP_NOP, OP_INC, OP_DEC, OP_DBL, OP_NEG, OP_SWAP, OP_CLR, OP_SET, OP_SKIP, OP_HALT };

NOINLINE static uint16_t dense(uint8_t v)
{
    switch (v) {
    case 0: return 11;
    case 1: return 22;
    case 2: return 33;
    case 3: return 44;
    case 4: return 55;
    case 5: return 66;
    case 6: return 77;
    case 7: return 88;
    case 8: return 99;
    case 9: return 110;
    case 10: return 121;
    case 11: return 132;
    default: return 0;
    }
}

NOINLINE static uint16_t sparse(uint16_t v)
{
    switch (v) {
    case 1: return 1;
    case 10: return 2;
    case 100: return 3;
    case 1000: return 4;
    case 10000: return 5;
    case 0x8000u: return 6;
    case 0xFFFFu: return 7;
    case 257: return 8;
    default: return 9;
    }
}

NOINLINE static uint16_t fall(uint8_t v)
{
    uint16_t r = 0;
    switch (v & 7) {
    case 0: r += 1;
    /* fall through */
    case 1: r += 2;
    /* fall through */
    case 2: r += 4; break;
    case 5:
    case 6: r = 100;
    /* fall through */
    default: r += 8;
    }
    return r;
}

NOINLINE static int16_t run(const uint8_t *prog, int16_t acc)
{
    int16_t other = 7;
    for (;;) {
        switch ((enum op)*prog++) {
        case OP_NOP: break;
        case OP_INC: acc++; break;
        case OP_DEC: acc--; break;
        case OP_DBL: acc = (int16_t)(acc * 2); break;
        case OP_NEG: acc = (int16_t)-acc; break;
        case OP_SWAP: { int16_t t = acc; acc = other; other = t; break; }
        case OP_CLR: acc = 0; break;
        case OP_SET: acc = (int16_t)*prog++; break;
        case OP_SKIP:
            switch (acc & 3) {
            case 0: prog++; break;
            case 3: prog += 2; break;
            default: break;
            }
            break;
        case OP_HALT: return (int16_t)(acc + other);
        }
    }
}

static const uint8_t program[] = {
    OP_SET, 20, OP_DBL, OP_INC, OP_SWAP, OP_DEC, OP_NEG, OP_SKIP, OP_CLR, OP_DBL,
    OP_SWAP, OP_INC, OP_SKIP, OP_NOP, OP_SET, 5, OP_DBL, OP_HALT,
};

void main(void)
{
    uint16_t a = 0, b = 0, c = 0;
    static const uint16_t probes[] = { 0, 1, 10, 100, 1000, 10000, 0x8000u, 0xFFFFu, 257, 256, 9999 };
    bench_init();
    for (uint8_t i = 0; i < 14; i++) a = (uint16_t)(a * 5u + dense(i));
    for (uint8_t i = 0; i < 11; i++) b = (uint16_t)(b * 10u + sparse(probes[i]));
    for (uint8_t i = 0; i < 8; i++) c = (uint16_t)(c * 3u + fall(i));
    bench_result16("dense", a);
    bench_result16("sparse", b);
    bench_result16("fall", c);
    bench_result16("vm", (uint16_t)run(program, 3));
    bench_exit(dense(11) == 132 && sparse(0x8000u) == 6 ? 0 : 1);
}
