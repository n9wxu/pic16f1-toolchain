/* Functions another file calls. Each has an array, so the kind of frame it
 * gets shows: on the stack, or at a fixed address. */
#include <stdint.h>
void fill(uint8_t *p, uint8_t v);
extern uint8_t depth;
uint8_t even(uint8_t n);

/* Called once from main: static frame. */
uint8_t plain(uint8_t x)
{
    uint8_t v[4];
    fill(v, x);
    return (uint8_t)(v[0] + v[3]);
}

/* Called from main and from the interrupt handler: stack frame. */
uint8_t shared(uint8_t x)
{
    uint8_t v[4];
    fill(v, x);
    return (uint8_t)(v[1] ^ v[2]);
}

/* odd and even (in main.c) call each other: stack frames. */
uint8_t odd(uint8_t n)
{
    uint8_t v[4];
    fill(v, n);
    depth++;
    return n ? even((uint8_t)(n - 1)) : (uint8_t)(v[0] & 0);
}

/* Reached only through a pointer, from a function that is not reached
 * again from here: static frame. */
uint8_t through_pointer(uint8_t x)
{
    uint8_t v[4];
    fill(v, x);
    return (uint8_t)(v[0] + 1);
}

/* Calls itself through a pointer it is handed: stack frame. */
uint8_t reenters(uint8_t (*f)(uint8_t), uint8_t n)
{
    uint8_t v[4];
    fill(v, n);
    return n ? f((uint8_t)(n - 1)) : v[0];
}
