/* Calls whose byte argument or result the compiler may move into W
 * (PIC16F1WConventions in the code generator): each shape is checked for
 * its value, whichever way it was compiled. For scripts/test-wcall.sh,
 * which also checks that the shapes meant to change do. */
#include <xc.h>

volatile unsigned char port = 3, seen, fed;

/* Returns constants: RETLW. */
__attribute__((noinline)) static unsigned char classify(unsigned char c)
{
    if (c < 10)
        return 0;
    if (c < 100)
        return 1;
    return 2;
}

/* The first block is the top of a loop. */
__attribute__((noinline)) static void put(unsigned char c)
{
    while (!(port & 1))
        ;
    seen += c;
}

__attribute__((noinline)) static unsigned char get(void)
{
    while (!(port & 2))
        ;
    return fed++;
}

__attribute__((noinline)) static unsigned char hex(unsigned char n)
{
    n &= 15;
    return n < 10 ? '0' + n : 'A' - 10 + n;
}

/* Several arguments: any one of them may be the one in W. */
__attribute__((noinline)) static unsigned char mix(unsigned char a, unsigned char b, unsigned char c)
{
    return (unsigned char)(a - b) ^ c;
}

/* A wider argument beside a byte, and a wider result. */
__attribute__((noinline)) static unsigned int scale(unsigned int x, unsigned char k)
{
    return x * k + 1;
}

/* Ends in a call of another: the two must agree on where the result is. */
__attribute__((noinline)) static unsigned char twice(unsigned char c)
{
    return hex(c + c);
}

/* Calls itself. */
__attribute__((noinline)) static unsigned char depth(unsigned char n)
{
    if (n == 0)
        return 1;
    return depth(n - 1) + n;
}

/* Called through a pointer as well: keeps the documented convention. */
__attribute__((noinline)) static unsigned char plus7(unsigned char c) { return c + 7; }
__attribute__((noinline)) static unsigned char minus7(unsigned char c) { return c - 7; }
static unsigned char (*volatile op)(unsigned char) = minus7;

/* The argument is used after a call. */
__attribute__((noinline)) static unsigned char keep(unsigned char c)
{
    put(1);
    return c + 1;
}

/* The result is not used. */
__attribute__((noinline)) static unsigned char count(unsigned char c)
{
    seen += c;
    return seen;
}

static unsigned char bad;
static void check(unsigned char got, unsigned char want)
{
    if (got != want)
        bad++;
}

void main(void)
{
    unsigned char i, c;
    for (i = 0; i != 3; i++) {
        c = get();                          /* 0, 1, 2 */
        put('>');
        put(hex(c + 9));                    /* '9', 'A', 'B' */
        if (classify(c * 50) == 1)          /* 0, 50, 100: only the second */
            put('!');
        put(mix(c, i, 3));                  /* 3 each time */
        put(twice(c));                      /* '0', '2', '4' */
    }
    /* 3*'>' + '9'+'A'+'B' + '!' + 9 + '0'+'2'+'4' */
    check(seen, (unsigned char)(3 * '>' + '9' + 'A' + 'B' + '!' + 9 + '0' + '2' + '4'));
    check(classify(9), 0);
    check(classify(10), 1);
    check(classify(200), 2);
    check(hex(0x2F), 'F');
    check(mix(200, 100, 0x0F), 100 ^ 0x0F);
    check(scale(300, 7) == 2101, 1);
    check(depth(5), 16);
    check(plus7(1), 8);
    check(op(10), 3);
    op = plus7;
    check(op(10), 17);
    seen = 0;
    check(keep(41), 42);
    check(seen, 1);
    count(5);
    check(seen, 6);
    check(count(get()), 9);                 /* get gives 3 */
    LATD = bad ? 1 : 0x5A;
    SLEEP();
}
