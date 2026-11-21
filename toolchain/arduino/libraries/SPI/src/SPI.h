// SPI: the MSSP as an SPI master (SCK on RC3, MISO on RC4, MOSI on RC5).
// Its clock is the oscillator divided by 4, 16 or 64; a transaction gets
// the fastest of those that is no faster than it asks.
//
// The MSSP is also what Wire uses; a sketch has one or the other.
#ifndef SPI_h
#define SPI_h

#include <Arduino.h>

#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3
#define SPI_CLOCK_DIV4 0
#define SPI_CLOCK_DIV16 1
#define SPI_CLOCK_DIV64 2

class SPISettings {
public:
    uint8_t divider, order, mode;
    constexpr SPISettings(unsigned long clock = 4000000, uint8_t bitOrder = MSBFIRST, uint8_t dataMode = SPI_MODE0)
        : divider(clock >= F_CPU / 4 ? 0 : clock >= F_CPU / 16 ? 1 : 2), order(bitOrder), mode(dataMode)
    {
    }
};

class SPIClass {
    uint8_t lsb_first = 0;

public:
    void begin();
    void end() { SSPCON1 = 0; }
    void beginTransaction(SPISettings settings);
    void endTransaction() {}
    uint8_t transfer(uint8_t data);
    uint16_t transfer16(uint16_t data);
    void transfer(void *buffer, size_t count);
    void setBitOrder(uint8_t order) { lsb_first = order == LSBFIRST; }
    void setDataMode(uint8_t mode);
    void setClockDivider(uint8_t divider) { SSPCON1 = (uint8_t)((SSPCON1 & 0xF0) | divider); }
};

extern SPIClass SPI;

#endif
