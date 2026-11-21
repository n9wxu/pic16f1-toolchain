// Print: text out of anything that can write a byte.
#ifndef Print_h
#define Print_h

#include <stddef.h>
#include <stdint.h>

class Print {
public:
    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t *buffer, size_t size);
    size_t write(const char *text) { return write((const uint8_t *)text, __builtin_strlen(text)); }
    size_t write(const char *buffer, size_t size) { return write((const uint8_t *)buffer, size); }

    size_t print(const char *text) { return write(text); }
    size_t print(char c) { return write((uint8_t)c); }
    size_t print(unsigned char n, uint8_t base = 10) { return print((unsigned int)n, base); }
    // (Numbers of 16 bits are printed with 16-bit arithmetic: a sketch
    // that prints no long does not carry the division of one.)
    size_t print(int n, uint8_t base = 10);
    size_t print(unsigned int n, uint8_t base = 10);
    size_t print(long n, uint8_t base = 10);
    size_t print(unsigned long n, uint8_t base = 10);
    size_t print(double n, uint8_t digits = 2);

    size_t println() { return write("\r\n"); }
    template <typename T> size_t println(T value) { return print(value) + println(); }
    template <typename T> size_t println(T value, uint8_t format) { return print(value, format) + println(); }

    virtual void flush() {}
};

#endif
