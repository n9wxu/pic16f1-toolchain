/* Variable-length arrays and alloca. */
extern void exit(int);
void *alloca(unsigned);
#define alloca __builtin_alloca
volatile unsigned char seven = 7, three = 3;

static unsigned sum(const unsigned char *p, unsigned char n)
{ unsigned s = 0; while (n--) s += *p++; return s; }

/* stack arguments: more than eight bytes of them */
__attribute__((noinline)) static unsigned many(unsigned a, unsigned b, unsigned c, unsigned d, unsigned e, unsigned f)
{ return a + b + c + d + e + f; }

#include <stdarg.h>
/* variadic: every argument after the count goes on the stack */
__attribute__((noinline)) static unsigned addup(unsigned char count, ...)
{
    va_list ap; unsigned s = 0;
    va_start(ap, count);
    while (count--) s += va_arg(ap, unsigned);
    va_end(ap);
    return s;
}

__attribute__((noinline)) unsigned vargs(unsigned char n)
{
    unsigned char local[3] = {seven, three, 1};
    unsigned char a[n];
    for (unsigned char i = 0; i < n; i++) a[i] = i + 1;
    /* arguments stored below the array, some read from it and the frame */
    unsigned s = addup(4, (unsigned)a[0], (unsigned)a[n - 1], (unsigned)local[0], (unsigned)local[1]);
    unsigned char b[n];
    for (unsigned char i = 0; i < n; i++) b[i] = 2;
    s += addup(3, (unsigned)b[0], (unsigned)a[1], (unsigned)local[2]);
    return s + sum(a, n) + sum(b, n) + local[0];
}

__attribute__((noinline)) unsigned vla(unsigned char n)
{
    unsigned char local[5] = {1, 2, 3, 4, 5};
    unsigned char a[n];
    for (unsigned char i = 0; i < n; i++) a[i] = i + 1;
    unsigned s = sum(a, n);                 /* a call, then the frame again */
    s += sum(local, 5);
    s += many(1, 2, 3, 4, a[n - 1], local[4]);  /* stack arguments below the array */
    return s + a[0] + local[0];
}

__attribute__((noinline)) unsigned nested(unsigned char n)
{
    unsigned char b[n];
    for (unsigned char i = 0; i < n; i++) b[i] = 10;
    unsigned s = vla(n);                    /* a callee with its own array */
    return s + sum(b, n);
}

__attribute__((noinline)) unsigned scoped(unsigned char n)
{
    unsigned total = 0;
    const unsigned char *first = 0;
    for (unsigned char r = 0; r < 4; r++) {
        unsigned char c[n];                 /* given back each time round */
        if (r == 0) first = c; else if (first != c) return 0xFFFF;
        for (unsigned char i = 0; i < n; i++) c[i] = r;
        total += sum(c, n);
    }
    return total;
}

__attribute__((noinline)) unsigned rec(unsigned char depth)
{
    unsigned char d[depth + 1];
    for (unsigned char i = 0; i <= depth; i++) d[i] = depth;
    unsigned below = depth ? rec(depth - 1) : 0;
    return below + sum(d, depth + 1);
}

__attribute__((noinline)) unsigned al(unsigned char n)
{
    unsigned total = 0;
    unsigned char *prev = 0;
    for (unsigned char r = 0; r < 3; r++) {
        unsigned char *p = alloca(n);       /* kept until the function returns */
        if (prev && p == prev) return 0xFFFF;
        for (unsigned char i = 0; i < n; i++) p[i] = r + 1;
        if (prev) total += sum(prev, n);
        prev = p;
    }
    return total + sum(prev, n);
}

int main(void)
{
    /* vla(7): 28 + 15 + (1+2+3+4+7+5) + 1 + 1 = 67 */
    if (vla(seven) != 67) exit(1);
    if (nested(seven) != 67 + 70) exit(2);
    if (scoped(three) != (0 + 1 + 2 + 3) * 3) exit(3);
    /* rec(3): 4*3 + 3*2 + 2*1 + 0 = 20 */
    if (rec(three) != 20) exit(4);
    /* al(3): (1 + 2 + 3) * 3 = 18 */
    if (al(three) != 18) exit(5);
    if (vla(seven) != 67) exit(6);
    /* vargs(7): (1+7+7+3) + (2+2+1) + 28 + 14 + 7 = 72 */
    if (vargs(seven) != 72) exit(7);
    exit(0);
}
