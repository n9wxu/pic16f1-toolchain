#include "Arduino.h"

static unsigned long seed = 1;

void randomSeed(unsigned long value)
{
    if (value)
        seed = value;
}
// (A linear congruential generator; the high bits are the good ones.)
long random(long limit)
{
    if (limit <= 0)
        return 0;
    seed = seed * 1103515245UL + 12345UL;
    return (long)((seed >> 1) % (unsigned long)limit);
}
long random(long low, long high)
{
    return high <= low ? low : low + random(high - low);
}
long map(long value, long fromLow, long fromHigh, long toLow, long toHigh)
{
    return (value - fromLow) * (toHigh - toLow) / (fromHigh - fromLow) + toLow;
}
unsigned int makeWord(uint8_t high, uint8_t low)
{
    return (unsigned int)high << 8 | low;
}
