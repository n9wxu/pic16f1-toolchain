/* setjmp and longjmp, and the builtin forms. The program exits with 0, or
 * the number of the check that failed. scripts/test-setjmp.sh runs it. */
#include <setjmp.h>
extern void exit(int);

volatile unsigned char three = 3, seven = 7;
static jmp_buf env, env2;
static void *benv[5];

/* Use the registers a function preserves, so that a jump over this
 * function skips their being restored. */
__attribute__((noinline)) static unsigned churn(unsigned a, unsigned b)
{
    unsigned x = a * 3, y = b * 5, z = a ^ b;
    for (unsigned char i = 0; i < three; i++) { x += y; y += z; z += x; }
    return x + y + z;
}

__attribute__((noinline)) static void deep(unsigned char n, int val)
{
    volatile unsigned char pad[4];
    pad[0] = n;
    if (n == 0)
        longjmp(env, val);
    unsigned k = churn(n, pad[0]);
    deep(n - 1, val);
    exit(90 + (k & 1));             /* never comes back */
}

/* 1: the value comes back, from several calls down. */
__attribute__((noinline)) static int basic(int val)
{
    int r = setjmp(env);
    if (r == 0) {
        deep(three, val);
        return -1;
    }
    return r;
}

/* 2: the caller's registers survive a jump over functions that used them. */
__attribute__((noinline)) static unsigned keeps(unsigned a, unsigned b)
{
    unsigned x = a + seven, y = b + three, z = a * b;   /* live across the call */
    int r = basic(5);
    return x + y + z + r;
}

/* 3: variable-length arrays made after setjmp are given back. */
__attribute__((noinline)) static void fills(unsigned char n)
{
    unsigned char a[n];
    for (unsigned char i = 0; i < n; i++) a[i] = i;
    if (a[n - 1] == n - 1)
        longjmp(env2, n);
}
__attribute__((noinline)) static const void *level(void)
{
    volatile unsigned char probe;
    probe = 0;
    return &probe;
}
__attribute__((noinline)) static int arrays(void)
{
    const void *before = level();
    volatile unsigned char rounds = 0;
    int r = setjmp(env2);
    if (r != 0 && level() != before)
        return -1;                  /* the stack did not come back */
    if (rounds < 5) {
        rounds++;
        fills(seven + rounds);
    }
    return r;                       /* 7 + 5 */
}

/* 4: the builtin forms. */
__attribute__((noinline)) static void bdeep(unsigned char n)
{
    if (n == 0)
        __builtin_longjmp(benv, 1);
    bdeep(n - 1);
}
__attribute__((noinline)) static int builtin(void)
{
    volatile int stage = 10;
    if (__builtin_setjmp(benv) == 0) {
        stage = 20;
        bdeep(three);
        return -1;
    }
    return stage;
}

int main(void)
{
    if (basic(42) != 42) exit(1);
    if (basic(0) != 1) exit(2);             /* 0 becomes 1 */
    /* (10+7) + (4+3) + 40 + 5 */
    if (keeps(10, 4) != 69) exit(3);
    if (arrays() != 12) exit(4);
    if (builtin() != 20) exit(5);
    /* Many times over: nothing is left on either stack. */
    for (unsigned char i = 0; i < 100; i++)
        if (basic(i + 1) != i + 1) exit(6);
    if (keeps(1, 2) != (1 + 7) + (2 + 3) + 2 + 5) exit(7);
    exit(0);
}
