/* A chain of calls DEPTH functions long below main, the last of which
 * divides (a call or a jump to the library), with an interrupt handler:
 * for scripts/test-stack.sh. */
#include <xc.h>
volatile unsigned char v;
unsigned char ticks;
__attribute__((noinline)) static unsigned char f0(unsigned char x) { return x / v; }
#define F(n, m) \
    __attribute__((noinline)) static unsigned char f##n(unsigned char x) { return f##m(x + v) + 1; }
F(1, 0) F(2, 1) F(3, 2) F(4, 3) F(5, 4) F(6, 5) F(7, 6) F(8, 7)
F(9, 8) F(10, 9) F(11, 10) F(12, 11) F(13, 12) F(14, 13) F(15, 14)
#define CAT(a, b) a##b
#define TOP(n) CAT(f, n)
void __attribute__((interrupt)) isr(void) { ticks++; }
void main(void)
{
    v = 1;
    LATD = TOP(DEPTH)(1);
    SLEEP();
}
