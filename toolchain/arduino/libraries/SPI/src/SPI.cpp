#include "SPI.h"

SPIClass SPI;

void SPIClass::begin()
{
    TRISCbits.TRISC3 = 0;
    TRISCbits.TRISC4 = 1;
    TRISCbits.TRISC5 = 0;
    beginTransaction(SPISettings());
}

// Mode 0 and 1 idle the clock low; 0 and 2 send on the edge back to idle
// (which this part calls CKE = 1).
void SPIClass::setDataMode(uint8_t mode)
{
    SSPCON1bits.CKP = (mode & 2) != 0;
    SSPSTATbits.CKE = !(mode & 1);
}

void SPIClass::beginTransaction(SPISettings settings)
{
    lsb_first = settings.order == LSBFIRST;
    SSPCON1 = 0;
    SSPSTAT = 0;
    setDataMode(settings.mode);
    SSPCON1 = (uint8_t)(SSPCON1 | 0x20 | settings.divider);
}

// (The part sends the high bit first and has no other way.)
static uint8_t reversed(uint8_t b)
{
    b = (uint8_t)((b & 0xF0) >> 4 | (b & 0x0F) << 4);
    b = (uint8_t)((b & 0xCC) >> 2 | (b & 0x33) << 2);
    return (uint8_t)((b & 0xAA) >> 1 | (b & 0x55) << 1);
}

uint8_t SPIClass::transfer(uint8_t data)
{
    SSPBUF = lsb_first ? reversed(data) : data;
    while (!SSPSTATbits.BF) {
    }
    uint8_t got = SSPBUF;
    return lsb_first ? reversed(got) : got;
}

uint16_t SPIClass::transfer16(uint16_t data)
{
    if (lsb_first) {
        uint8_t low = transfer((uint8_t)data);
        return (uint16_t)(transfer((uint8_t)(data >> 8)) << 8 | low);
    }
    uint8_t high = transfer((uint8_t)(data >> 8));
    return (uint16_t)(high << 8 | transfer((uint8_t)data));
}

void SPIClass::transfer(void *buffer, size_t count)
{
    for (uint8_t *p = (uint8_t *)buffer; count--; p++)
        *p = transfer(*p);
}
