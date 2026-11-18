/* CRC-16/CCITT-FALSE, bit at a time. Check value for "123456789" is 0x29B1. */
#include "bench.h"

static const char check[] = "123456789";
static uint8_t buf[64];

static uint16_t crc16(const uint8_t *p, uint8_t n)
{
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= (uint16_t)((uint16_t)*p++ << 8);
        for (uint8_t i = 0; i < 8; i++) {
            if (crc & 0x8000) {
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

void main(void)
{
    uint16_t a, b;
    bench_init();
    a = crc16((const uint8_t *)check, 9);
    for (uint8_t i = 0; i < 64; i++) {
        buf[i] = (uint8_t)(i * 7u + 3u);
    }
    b = crc16(buf, 64);
    bench_result16("check", a);
    bench_result16("buf", b);
    bench_exit(a == 0x29B1 ? 0 : 1);
}
