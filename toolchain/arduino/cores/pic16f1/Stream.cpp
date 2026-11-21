#include "Arduino.h"

int Stream::timedRead()
{
    unsigned long began = millis();
    do {
        int c = read();
        if (c >= 0)
            return c;
    } while (millis() - began < timeout);
    return -1;
}

int Stream::timedPeek()
{
    unsigned long began = millis();
    do {
        int c = peek();
        if (c >= 0)
            return c;
    } while (millis() - began < timeout);
    return -1;
}

size_t Stream::readBytes(char *buffer, size_t length)
{
    size_t count = 0;
    while (count < length) {
        int c = timedRead();
        if (c < 0)
            break;
        buffer[count++] = (char)c;
    }
    return count;
}

size_t Stream::readBytesUntil(char terminator, char *buffer, size_t length)
{
    size_t count = 0;
    while (count < length) {
        int c = timedRead();
        if (c < 0 || c == terminator)
            break;
        buffer[count++] = (char)c;
    }
    return count;
}

long Stream::parseInt()
{
    int c;
    bool negative = false;
    for (;;) {
        c = timedPeek();
        if (c < 0)
            return 0;
        if (c == '-' || (c >= '0' && c <= '9'))
            break;
        read();
    }
    long value = 0;
    do {
        if (c == '-')
            negative = true;
        else
            value = value * 10 + (c - '0');
        read();
        c = timedPeek();
    } while (c >= '0' && c <= '9');
    return negative ? -value : value;
}
