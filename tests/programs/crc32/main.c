/* CRC-32 (reflected, poly 0xEDB88320), bit at a time. Check value 0xCBF43926. */
#include "bench.h"

static const char check[] = "123456789";

static uint32_t crc32(const uint8_t *p, uint8_t n)
{
    uint32_t crc = 0xFFFFFFFFUL;
    while (n--) {
        crc ^= *p++;
        for (uint8_t i = 0; i < 8; i++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320UL;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

void main(void)
{
    uint32_t a;
    bench_init();
    a = crc32((const uint8_t *)check, 9);
    bench_result32("check", a);
    bench_exit(a == 0xCBF43926UL ? 0 : 1);
}
