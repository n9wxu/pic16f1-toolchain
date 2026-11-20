/* Float arithmetic on the target, against results the compiler works out
 * itself (exactly, at compile time) from the same constants. The exit code
 * is a mask of what went wrong: 1 add, 2 subtract, 4 multiply, 8 divide,
 * 16 compare, 32 integer to float, 64 float to integer. For
 * scripts/test-float.sh. */
#include <xc.h>
typedef union { float f; unsigned long b; } pun;
volatile float a, b; volatile long i; volatile unsigned long u;
static unsigned char bad;
#define CHECK(n, expr, X, Y, want) do { a = (X); b = (Y); pun got; got.f = (expr); pun w; w.f = (want); if (got.b != w.b) bad |= (n); } while (0)
#define T(X, Y) \
    CHECK(1, a + b, X, Y, (X) + (Y)); CHECK(2, a - b, X, Y, (X) - (Y)); \
    CHECK(4, a * b, X, Y, (X) * (Y)); CHECK(8, a / b, X, Y, (X) / (Y)); \
    a = (X); b = (Y); if ((a < b) != ((X) < (Y))) bad |= 16; if ((a >= b) != ((X) >= (Y))) bad |= 16; if ((a == b) != ((X) == (Y))) bad |= 16;
void main(void)
{
    T(3.25f, -1700.0f) T(1.0f, 3.0f) T(1e-20f, 1e-20f) T(16777216.0f, 1.0f) T(0.1f, 0.2f) T(-5.5f, -5.5f) T(1e30f, 1e10f)
    i = -123456; { pun g; g.f = i; pun w; w.f = -123456.0f; if (g.b != w.b) bad |= 32; }
    u = 3000000000u; { pun g; g.f = u; pun w; w.f = 3000000000.0f; if (g.b != w.b) bad |= 32; }
    a = -77.9f; if ((long)a != -77) bad |= 64; a = 3e9f; if ((unsigned long)a != 3000000000u) bad |= 64;
    a = 0.75f; if ((long)a != 0) bad |= 64; a = 16777217.0f; if ((long)a != 16777216) bad |= 64;
    LATD = bad;
    SLEEP();
}
