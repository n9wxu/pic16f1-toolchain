// Wire: the MSSP as an I2C master (SCL on RC3, SDA on RC4; the board
// needs pull-up resistors). One buffer of 32 bytes serves both directions.
// No wait is without end: a bus that is stuck gives error 5.
//
// The MSSP is also what SPI uses; a sketch has one or the other.
#ifndef TwoWire_h
#define TwoWire_h

#include <Arduino.h>

#define BUFFER_LENGTH 32

class TwoWire : public Stream {
    uint8_t buffer[BUFFER_LENGTH];
    uint8_t length = 0; // bytes in the buffer
    uint8_t index = 0;  // the next to read
    uint8_t address = 0;

public:
    void begin();
    void end();
    // (Inline, for the compiler to do the division of a constant rate.)
    inline void setClock(unsigned long hz) { SSPADD = (uint8_t)(F_CPU / (4UL * hz) - 1); }

    void beginTransmission(uint8_t address);
    // 0: sent. 2: no device answered the address. 3: a byte was refused.
    // 5: the bus did not respond.
    uint8_t endTransmission(bool stop = true);
    // How many bytes arrived.
    uint8_t requestFrom(uint8_t address, uint8_t quantity, bool stop = true);

    size_t write(uint8_t) override;
    using Print::write;
    // (So that write(0) is a byte and not a null string.)
    size_t write(int n) { return write((uint8_t)n); }
    size_t write(unsigned int n) { return write((uint8_t)n); }
    size_t write(long n) { return write((uint8_t)n); }
    size_t write(unsigned long n) { return write((uint8_t)n); }
    int available() override { return length - index; }
    int read() override { return index < length ? buffer[index++] : -1; }
    int peek() override { return index < length ? buffer[index] : -1; }
};

extern TwoWire Wire;

#endif
