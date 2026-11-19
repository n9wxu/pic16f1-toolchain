/* 64-bit integer multiply and divide routines. The 8-, 16- and 32-bit ones
 * are in assembly, in muldiv.S. Plain shift-and-add and restoring division.
 *
 * Only one-bit constant shifts are used, which the compiler expands inline,
 * so nothing here calls back into this file.
 */

typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
typedef unsigned long long u64;
typedef signed char s8;
typedef int s16;
typedef long s32;
typedef long long s64;

#define MUL(T, name)                                                           \
    T name(T a, T b)                                                           \
    {                                                                          \
        T r = 0;                                                               \
        while (b) {                                                            \
            if (b & 1)                                                         \
                r += a;                                                        \
            a <<= 1;                                                           \
            b >>= 1;                                                           \
        }                                                                      \
        return r;                                                              \
    }

MUL(u64, __muldi3)

/* Restoring division. The remainder can need one bit more than T while it
 * is being shifted; `carry` holds that bit. */
#define UDIVMOD(T, BITS, TOP, name)                                            \
    static T name(T n, T d, T *rem)                                            \
    {                                                                          \
        T q = 0, r = 0;                                                        \
        for (u8 i = BITS; i != 0; i--) {                                       \
            u8 carry = (r & TOP) != 0;                                         \
            r <<= 1;                                                           \
            if (n & TOP)                                                       \
                r |= 1;                                                        \
            n <<= 1;                                                           \
            q <<= 1;                                                           \
            if (carry || r >= d) {                                             \
                r -= d;                                                        \
                q |= 1;                                                        \
            }                                                                  \
        }                                                                      \
        *rem = r;                                                              \
        return q;                                                              \
    }

UDIVMOD(u64, 64, 0x8000000000000000ULL, udivmod64)

#define DIVMOD(U, S, core, udiv, umod, sdiv, smod)                             \
    U udiv(U a, U b)                                                           \
    {                                                                          \
        U r;                                                                   \
        return core(a, b, &r);                                                 \
    }                                                                          \
    U umod(U a, U b)                                                           \
    {                                                                          \
        U r;                                                                   \
        core(a, b, &r);                                                        \
        return r;                                                              \
    }                                                                          \
    S sdiv(S a, S b)                                                           \
    {                                                                          \
        u8 neg = 0;                                                            \
        U ua = (U)a, ub = (U)b, r, q;                                          \
        if (a < 0) {                                                           \
            ua = (U)0 - ua;                                                    \
            neg = 1;                                                           \
        }                                                                      \
        if (b < 0) {                                                           \
            ub = (U)0 - ub;                                                    \
            neg ^= 1;                                                          \
        }                                                                      \
        q = core(ua, ub, &r);                                                  \
        return (S)(neg ? (U)0 - q : q);                                        \
    }                                                                          \
    S smod(S a, S b)                                                           \
    {                                                                          \
        u8 neg = 0;                                                            \
        U ua = (U)a, ub = (U)b, r;                                             \
        if (a < 0) {                                                           \
            ua = (U)0 - ua;                                                    \
            neg = 1;                                                           \
        }                                                                      \
        if (b < 0)                                                             \
            ub = (U)0 - ub;                                                    \
        core(ua, ub, &r);                                                      \
        return (S)(neg ? (U)0 - r : r);                                        \
    }

DIVMOD(u64, s64, udivmod64, __udivdi3, __umoddi3, __divdi3, __moddi3)
