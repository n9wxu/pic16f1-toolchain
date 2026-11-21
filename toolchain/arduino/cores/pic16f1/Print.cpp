#include "Print.h"

size_t Print::write(const uint8_t *buffer, size_t size)
{
    for (size_t i = 0; i < size; i++)
        write(buffer[i]);
    return size;
}

// Digits are made last first, into the end of a buffer.
template <typename T> static size_t number(Print &out, T n, uint8_t base)
{
    char text[8 * sizeof(T) + 1];
    char *p = text + sizeof text;
    if (base < 2)
        base = 10;
    do {
        uint8_t digit = (uint8_t)(n % base);
        n /= base;
        *--p = (char)(digit < 10 ? '0' + digit : 'A' + digit - 10);
    } while (n);
    return out.write((const uint8_t *)p, (size_t)(text + sizeof text - p));
}

size_t Print::print(unsigned int n, uint8_t base)
{
    return number<unsigned int>(*this, n, base);
}
size_t Print::print(int n, uint8_t base)
{
    if (base == 10 && n < 0)
        return write((uint8_t)'-') + number<unsigned int>(*this, 0u - (unsigned int)n, 10);
    return number<unsigned int>(*this, (unsigned int)n, base);
}
size_t Print::print(unsigned long n, uint8_t base)
{
    return number<unsigned long>(*this, n, base);
}
size_t Print::print(long n, uint8_t base)
{
    if (base == 10 && n < 0)
        return write((uint8_t)'-') + number<unsigned long>(*this, 0ul - (unsigned long)n, 10);
    return number<unsigned long>(*this, (unsigned long)n, base);
}

size_t Print::print(double n, uint8_t digits)
{
    size_t count = 0;
    if (n != n)
        return write("nan");
    if (n < 0) {
        count += write((uint8_t)'-');
        n = -n;
    }
    if (n > 4294967040.0)
        return count + write("ovf");
    // Rounded at the last digit shown.
    double rounding = 0.5;
    for (uint8_t i = 0; i < digits; i++)
        rounding /= 10.0;
    n += rounding;
    unsigned long whole = (unsigned long)n;
    double rest = n - (double)whole;
    count += print(whole);
    if (digits)
        count += write((uint8_t)'.');
    while (digits--) {
        rest *= 10.0;
        uint8_t digit = (uint8_t)rest;
        count += write((uint8_t)('0' + digit));
        rest -= digit;
    }
    return count;
}
