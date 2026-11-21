// Serial: the EUSART (TX on RC6, RX on RC7), 8 data bits, no parity.
// Received bytes are taken by the interrupt handler into a buffer of 16;
// sending waits for the transmitter.
#ifndef HardwareSerial_h
#define HardwareSerial_h

#include "Stream.h"

#define SERIAL_RX_BUFFER_SIZE 16
#define SERIAL_8N1 0

class HardwareSerial : public Stream {
    void start(uint16_t divisor);

public:
    constexpr HardwareSerial() {}
    // (Inline, so that the divisor of a constant rate is worked out by
    // the compiler.)
    inline void begin(unsigned long baud, uint8_t = SERIAL_8N1)
    {
        start((uint16_t)((F_CPU / 4 + baud / 2) / baud - 1));
    }
    void end();
    int available() override;
    int peek() override;
    int read() override;
    int availableForWrite();
    void flush() override;
    size_t write(uint8_t) override;
    using Print::write;
    // (So that write(0) is a byte and not a null string.)
    size_t write(int n) { return write((uint8_t)n); }
    size_t write(unsigned int n) { return write((uint8_t)n); }
    size_t write(long n) { return write((uint8_t)n); }
    size_t write(unsigned long n) { return write((uint8_t)n); }
    operator bool() { return true; }
};

extern HardwareSerial Serial;

#endif
