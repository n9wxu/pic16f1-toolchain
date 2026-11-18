/* Pointer arithmetic, comparison and indirection, and the memory routines. */
#include "bench.h"
#define NOINLINE __attribute__((noinline))

#ifdef __PIC16F1__
void *memcpy(void *, const void *, unsigned int);
void *memmove(void *, const void *, unsigned int);
void *memset(void *, int, unsigned int);
int memcmp(const void *, const void *, unsigned int);
#else
#include <string.h>
#endif

struct rec { uint8_t key; uint16_t val; uint8_t pad[3]; };

static uint16_t buf[24];
static struct rec recs[6];
static uint8_t bytes[48];
static const uint8_t pattern[8] = { 3, 1, 4, 1, 5, 9, 2, 6 };

NOINLINE static uint16_t *find(uint16_t *first, uint16_t *last, uint16_t v)
{
    while (first != last && *first != v) first++;
    return first;
}

NOINLINE static void swap(uint16_t **a, uint16_t **b)
{
    uint16_t *t = *a;
    *a = *b;
    *b = t;
}

NOINLINE static struct rec *best(struct rec *r, uint8_t n)
{
    struct rec *m = r;
    for (struct rec *p = r + 1; p < r + n; p++)
        if (p->val > m->val) m = p;
    return m;
}

NOINLINE static uint16_t walk_back(const uint8_t *end, uint8_t n)
{
    uint16_t s = 0;
    while (n--) s = (uint16_t)(s * 2u + *--end);
    return s;
}

static uint16_t twice(uint16_t x) { return (uint16_t)(x * 2u); }
static uint16_t plus7(uint16_t x) { return (uint16_t)(x + 7u); }
static uint16_t (*handlers[3])(uint16_t);

void main(void)
{
    uint16_t *p, *q, *lo = &buf[2], *hi = &buf[20];
    struct rec *m, copy;
    uint16_t r = 0;
    bench_init();
    for (uint8_t i = 0; i < 24; i++) buf[i] = (uint16_t)(i * i + 3u);
    p = find(buf, buf + 24, 124);
    q = find(buf, buf + 24, 9999);
    bench_result16("found", (uint16_t)((p - buf) * 100 + (q - buf)));
    swap(&lo, &hi);
    bench_result16("swap", (uint16_t)((lo - hi) + *lo + hi[-1] + (lo > hi)));

    for (uint8_t i = 0; i < 6; i++) {
        recs[i].key = i;
        recs[i].val = (uint16_t)((i * 7919u) % 1000u);
    }
    m = best(recs, 6);
    copy = *m;
    copy.val++;
    recs[0] = copy;
    bench_result16("best", (uint16_t)(m->key * 1000u + recs[0].val - m->val + (uint16_t)(m - recs)));

    memset(bytes, 0xA5, sizeof bytes);
    memcpy(bytes + 4, pattern, 8);
    memcpy(bytes + 20, bytes + 4, 8);
    memmove(bytes + 6, bytes + 4, 8);
    for (uint8_t i = 0; i < 48; i++) r = (uint16_t)(r * 3u + bytes[i]);
    bench_result16("mem", r);
    bench_result16("cmp", (uint16_t)((memcmp(bytes + 20, pattern, 8) == 0) |
                                     ((memcmp(bytes + 4, pattern, 8) != 0) << 1) |
                                     ((memcmp("abc", "abd", 3) < 0) << 2)));
    bench_result16("back", walk_back(pattern + 8, 8));

    handlers[0] = twice;
    handlers[1] = plus7;
    handlers[2] = twice;
    r = 5;
    for (uint8_t i = 0; i < 6; i++) r = handlers[i % 3](r);
    bench_result16("calls", r);
    bench_exit(p == &buf[11] && q == buf + 24 ? 0 : 1);
}
