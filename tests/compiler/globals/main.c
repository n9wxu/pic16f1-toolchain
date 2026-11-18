/* Initialised and zeroed globals, arrays that cross bank boundaries,
 * constant tables in flash, and tables of pointers. */
#include "bench.h"
#define NOINLINE __attribute__((noinline))

static uint8_t big[200];                 /* spans three banks of RAM */
static uint16_t words[40];
static uint16_t init_words[6] = { 100, 200, 300, 400, 500, 60000u };
static uint8_t init_bytes[] = "initialised";
static uint32_t counter = 0x12345678UL;
static const uint16_t rom_table[8] = { 1, 10, 100, 1000, 10000, 3, 33, 333 };
static const char *const names[4] = { "zero", "one", "two", "three" };
static const uint8_t matrix[3][4] = { { 1, 2, 3, 4 }, { 5, 6, 7, 8 }, { 9, 10, 11, 12 } };
static uint8_t *cursor = &big[150];
static const uint16_t *const rom_ptr = &rom_table[4];
int16_t exported = -1234;
static volatile uint8_t one = 1;

NOINLINE static uint8_t next(void)
{
    static uint8_t state = 17;
    state = (uint8_t)(state * 5u + 3u);
    return state;
}

void main(void)
{
    uint16_t s = 0, t = 0, u = 0;
    bench_init();
    for (uint8_t i = 0; i < 200; i++) big[i] = (uint8_t)(i ^ next());
    for (uint8_t i = 0; i < 40; i++) words[i] = (uint16_t)(big[i * 5u] * 300u + i);
    for (uint8_t i = 0; i < 200; i++) s = (uint16_t)(s * 3u + big[i]);
    for (uint8_t i = 0; i < 40; i++) t ^= (uint16_t)(words[i] + (words[39 - i] >> 3));
    bench_result16("big", s);
    bench_result16("words", t);

    for (uint8_t i = 0; i < 6; i++) u = (uint16_t)(u + init_words[i]);
    init_words[2] = (uint16_t)(init_words[2] + one);
    bench_result16("init", (uint16_t)(u + init_words[2]));
    bench_puts((const char *)init_bytes);
    bench_putc('\n');
    counter += 0x11111111UL * one;
    bench_result32("counter", counter);

    u = 0;
    for (uint8_t i = 0; i < 8; i++) u = (uint16_t)(u + rom_table[i]);
    bench_result16("rom", (uint16_t)(u + *rom_ptr + rom_ptr[-1]));
    for (uint8_t i = 0; i < 4; i++) {
        bench_puts(names[(uint8_t)(i + one) & 3]);
        bench_putc(i == 3 ? '\n' : ' ');
    }
    u = 0;
    for (uint8_t r = 0; r < 3; r++)
        for (uint8_t c = 0; c < 4; c++) u = (uint16_t)(u * 2u + matrix[r][3 - c]);
    bench_result16("matrix", u);

    *cursor = 0xEE;
    cursor[-1] = 0x11;
    cursor += 10;
    *cursor = 0x22;
    bench_result16("cursor", (uint16_t)(big[149] + big[150] + big[160]));
    exported = (int16_t)(exported * 2);
    bench_result16("exported", (uint16_t)exported);
    bench_exit(counter == 0x23456789UL ? 0 : 1);
}
