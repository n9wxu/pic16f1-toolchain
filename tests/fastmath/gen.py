#!/usr/bin/env python3
"""Write a C program that checks integer and floating-point arithmetic on
the target against results worked out here: gen.py [seed] [count] [int] > t.c
(with "int", the integer part only, which fits a program of one page)

It is for the fast arithmetic library (-mmath=fast), and is run against
the standard one too, which checks the program. Operands come in the
kinds that take different ways through the routines: divisors of one, two
and four bytes, multipliers with zero bytes, floats close together and far
apart, with simple and with full significands, subnormals, infinities and
NaNs. A float result is computed in double and rounded once, which is
exact for these four operations. The program ends with 0x5A in LATD, or
the number of the kind that failed first (and the index in `failed_at`).
"""
import random
import struct
import sys

seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
count = int(sys.argv[2]) if len(sys.argv) > 2 else 40
integers_only = sys.argv[3:4] == ["int"]
rnd = random.Random(seed)
M32 = 0xFFFFFFFF


def u32():
    kind = rnd.randrange(6)
    if kind == 0:
        return rnd.randrange(256)
    if kind == 1:
        return rnd.randrange(65536)
    if kind == 2:
        return rnd.randrange(1 << 24)
    if kind == 3:
        return rnd.randrange(256) << (8 * rnd.randrange(4))
    return rnd.getrandbits(32)


def bits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def value(b):
    return struct.unpack("<f", struct.pack("<I", b))[0]


def f32():
    kind = rnd.randrange(8)
    if kind == 0:
        return bits(float(rnd.randrange(-1000, 1000)))
    if kind == 1:
        return bits(rnd.choice([0.5, 2.0, 10.0, 0.1, 3.0, 1.0, -1.0, 1e-3, 255.0]))
    if kind == 2:   # exponents close to 1: sums cancel and round
        return (rnd.getrandbits(1) << 31) | ((127 + rnd.randrange(-3, 4)) << 23) | rnd.getrandbits(23)
    if kind == 3:   # subnormal, or nearly
        return (rnd.getrandbits(1) << 31) | (rnd.randrange(3) << 23) | rnd.getrandbits(23)
    if kind == 4:   # large
        return (rnd.getrandbits(1) << 31) | (rnd.randrange(250, 255) << 23) | rnd.getrandbits(23)
    if kind == 5:
        return rnd.choice([0, 0x80000000, 0x7F800000, 0xFF800000, 0x7FC00000, 0x3F800000, 0x00000001, 0x7F7FFFFF])
    return rnd.getrandbits(32)


def is_nan(b):
    return (b & 0x7FFFFFFF) > 0x7F800000


def rounded(x):
    """A double as the bits of the nearest float."""
    try:
        return bits(x)
    except OverflowError:
        return 0x7F800000 if x > 0 else 0xFF800000


def float_op(op, a, b):
    x, y = value(a), value(b)
    try:
        if op == "+":
            r = x + y
        elif op == "-":
            r = x - y
        elif op == "*":
            r = x * y
        else:
            if y == 0:
                if x == 0 or x != x:
                    return 0x7FC00000
                sign = (a ^ b) & 0x80000000
                return sign | 0x7F800000
            r = x / y
    except OverflowError:
        r = float("inf")
    if r != r:
        return 0x7FC00000
    return rounded(r)


def table(name, kind, rows):
    out = [f"static const {kind} {name}[][{len(rows[0])}] = {{"]
    for r in rows:
        out.append("    {" + ", ".join(f"0x{v:X}UL" if kind.endswith("long") else f"0x{v:X}u" for v in r) + "},")
    out.append("};")
    return "\n".join(out)


def trunc_div(a, b):
    q = abs(a) // abs(b)
    if (a < 0) != (b < 0):
        q = -q
    return q, a - q * b


def signed(v, n):
    return v - (1 << n) if v >> (n - 1) else v


mul32 = [(a, b, (a * b) & M32) for a, b in ((u32(), u32()) for _ in range(count))]
div32 = []
for _ in range(count):
    a, b = u32(), u32() or 1
    div32.append((a, b, a // b, a % b))
sdiv32 = []
for _ in range(count // 2):
    a, b = u32(), u32() or 1
    q, r = trunc_div(signed(a, 32), signed(b, 32))
    if not -(1 << 31) <= q < (1 << 31):
        continue
    sdiv32.append((a, b, q & M32, r & M32))
mul16 = [(a, b, (a * b) & 0xFFFF) for a, b in ((u32() & 0xFFFF, u32() & 0xFFFF) for _ in range(count))]
floats = []
for _ in range(count):
    a, b = f32(), f32()
    floats.append((a, b, float_op("+", a, b), float_op("-", a, b), float_op("*", a, b), float_op("/", a, b)))
ints = [v for v in (u32() for _ in range(count))]
from_int = [(v, rounded(float(v)), rounded(float(signed(v, 32)))) for v in ints]

FLOAT_LOOPS = """    for (i = 0; i < ROWS(floats); i++) {
        pun a, b, r;
        x = floats[i][0]; y = floats[i][1];
        a.b = x; b.b = y;
        r.f = a.f + b.f; if (!same(r.b, floats[i][2])) FAIL(7, i);
        r.f = a.f - b.f; if (!same(r.b, floats[i][3])) FAIL(8, i);
        r.f = a.f * b.f; if (!same(r.b, floats[i][4])) FAIL(9, i);
        r.f = a.f / b.f; if (!same(r.b, floats[i][5])) FAIL(10, i);
    }
    for (i = 0; i < ROWS(from_int); i++) {
        pun r;
        x = from_int[i][0];
        r.f = (float)x; if (r.b != from_int[i][1]) FAIL(11, i);
        r.f = (float)(long)x; if (r.b != from_int[i][2]) FAIL(12, i);
    }
"""

print(f"""/* Made by tests/fastmath/gen.py {seed} {count}: do not edit. */
#include <xc.h>
typedef union {{ float f; unsigned long b; }} pun;
volatile unsigned long x, y;
volatile unsigned int p, q;
volatile unsigned char failed;
volatile unsigned int failed_at;
#define FAIL(n, i) do {{ if (!failed) {{ failed = (n); failed_at = (i); }} }} while (0)
#define ROWS(t) (sizeof (t) / sizeof (t)[0])
static unsigned char same(unsigned long got, unsigned long want)
{{
    /* (A NaN is a NaN, whatever its other bits.) */
    if ((want & 0x7FFFFFFFUL) > 0x7F800000UL)
        return (got & 0x7FFFFFFFUL) > 0x7F800000UL;
    return got == want;
}}
{table("mul32", "unsigned long", mul32)}
{table("div32", "unsigned long", div32)}
{table("sdiv32", "unsigned long", sdiv32)}
{table("mul16", "unsigned int", mul16)}
{"" if integers_only else table("floats", "unsigned long", floats)}
{"" if integers_only else table("from_int", "unsigned long", from_int)}
void main(void)
{{
    unsigned int i;
    for (i = 0; i < ROWS(mul32); i++) {{
        x = mul32[i][0]; y = mul32[i][1];
        if (x * y != mul32[i][2]) FAIL(1, i);
    }}
    for (i = 0; i < ROWS(div32); i++) {{
        x = div32[i][0]; y = div32[i][1];
        if (x / y != div32[i][2]) FAIL(2, i);
        if (x % y != div32[i][3]) FAIL(3, i);
    }}
    for (i = 0; i < ROWS(sdiv32); i++) {{
        x = sdiv32[i][0]; y = sdiv32[i][1];
        if ((unsigned long)((long)x / (long)y) != sdiv32[i][2]) FAIL(4, i);
        if ((unsigned long)((long)x % (long)y) != sdiv32[i][3]) FAIL(5, i);
    }}
    for (i = 0; i < ROWS(mul16); i++) {{
        p = mul16[i][0]; q = mul16[i][1];
        if ((unsigned int)(p * q) != mul16[i][2]) FAIL(6, i);
    }}
{"" if integers_only else FLOAT_LOOPS}    LATD = failed ? failed : 0x5A;
    SLEEP();
}}""")
