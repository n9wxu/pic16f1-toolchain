/* Functions with variables in memory, called in chains, as siblings,
 * through a pointer and from an interrupt handler. Each fills its
 * variables with a pattern of its own, calls on, and checks the pattern
 * afterwards: a frame that shares memory with one that is active at the
 * same time shows up as a wrong pattern. For scripts/test-overlay.sh, which
 * also checks that the frames do share. */
#include <xc.h>

volatile unsigned char bad, ticks, handler_runs;

#define FILL(a, seed) do { unsigned char i_; for (i_ = 0; i_ < sizeof a; i_++) a[i_] = (unsigned char)(seed + i_ * 7); } while (0)
#define CHECK(a, seed, bit) do { unsigned char i_; for (i_ = 0; i_ < sizeof a; i_++) if (a[i_] != (unsigned char)(seed + i_ * 7)) bad |= bit; } while (0)
#define NOINLINE __attribute__((noinline))

/* The handler's own callee, with a frame: it must not land on anything the
 * main program has. */
NOINLINE static void in_handler(unsigned char seed)
{
    volatile unsigned char h[9];
    FILL(h, seed);
    CHECK(h, seed, 0x80);
    handler_runs++;
}

void __attribute__((interrupt)) isr(void)
{
    if (INTCONbits.TMR0IF) {
        INTCONbits.TMR0IF = 0;
        ticks++;
        in_handler(ticks);
    }
}

NOINLINE static void leaf_a(unsigned char seed)
{
    volatile unsigned char a[12];
    FILL(a, seed);
    while (ticks < 2)       /* let the handler run while this frame is live */
        ;
    CHECK(a, seed, 1);
}

NOINLINE static void leaf_b(unsigned char seed)
{
    volatile unsigned char b[10];
    FILL(b, seed);
    CHECK(b, seed, 2);
}

/* Siblings under one parent: they may share with each other, not with it. */
NOINLINE static void middle(unsigned char seed)
{
    volatile unsigned char m[8];
    FILL(m, seed);
    leaf_a(seed + 1);
    CHECK(m, seed, 4);
    leaf_b(seed + 2);
    CHECK(m, seed, 4);
}

NOINLINE static void other(unsigned char seed)
{
    volatile unsigned char o[14];
    FILL(o, seed);
    leaf_b(seed + 3);       /* leaf_b under a second parent, with a larger frame */
    CHECK(o, seed, 8);
}

/* Reached only through a pointer. */
NOINLINE static void by_pointer(unsigned char seed)
{
    volatile unsigned char p[6];
    FILL(p, seed);
    leaf_b(seed + 5);
    CHECK(p, seed, 16);
}

static void (*volatile hook)(unsigned char) = by_pointer;

NOINLINE static void top(unsigned char seed)
{
    volatile unsigned char t[5];
    FILL(t, seed);
    middle(seed + 10);
    CHECK(t, seed, 32);
    other(seed + 20);
    CHECK(t, seed, 32);
    hook(seed + 30);
    CHECK(t, seed, 32);
}

void main(void)
{
    unsigned char round;
    OPTION_REG = 0x83;          /* timer 0 from the instruction clock, 1:16 */
    TMR0 = 0;
    INTCONbits.TMR0IF = 0;
    INTCONbits.TMR0IE = 1;
    INTCONbits.GIE = 1;
    for (round = 0; round < 3; round++) {
        ticks = 0;
        top(round * 40 + 3);
    }
    INTCONbits.GIE = 0;
    LATD = bad ? bad : handler_runs >= 6 ? 0x5A : 0x40;
    SLEEP();
}
