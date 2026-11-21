#include "Wire.h"

TwoWire Wire;

// Until the module has finished what it was told to do, or about 2 ms.
static bool settled()
{
    for (uint8_t patience = 255; patience; patience--) {
        if (!(SSPCON2 & 0x1F) && !SSPSTATbits.R_nW)
            return true;
        __delay_us(8);
    }
    return false;
}

static bool send(uint8_t byte)
{
    SSPBUF = byte;
    return settled();
}

static void stop_bus()
{
    SSPCON2bits.PEN = 1;
    settled();
}

void TwoWire::begin()
{
    SSPCON1 = 0;
    TRISCbits.TRISC3 = 1;
    TRISCbits.TRISC4 = 1;
    // A device that was cut off in the middle of a byte holds the data
    // line low until it has been clocked through the rest of it.
    LATCbits.LATC3 = 0;
    for (uint8_t i = 0; i < 9 && !PORTCbits.RC4; i++) {
        TRISCbits.TRISC3 = 0;
        __delay_us(5);
        TRISCbits.TRISC3 = 1;
        __delay_us(5);
    }
    setClock(100000);
    SSPSTAT = 0x80;
    SSPCON2 = 0;
    SSPCON1 = 0x28;
}

void TwoWire::end()
{
    SSPCON1 = 0;
}

void TwoWire::beginTransmission(uint8_t to)
{
    address = to;
    length = index = 0;
}

size_t TwoWire::write(uint8_t byte)
{
    if (length >= BUFFER_LENGTH)
        return 0;
    buffer[length++] = byte;
    return 1;
}

uint8_t TwoWire::endTransmission(bool stop)
{
    uint8_t result = 0;
    // (A start while the bus is still held from the last time is a
    // repeated start.)
    if (SSPSTATbits.S && !SSPSTATbits.P)
        SSPCON2bits.RSEN = 1;
    else
        SSPCON2bits.SEN = 1;
    if (!settled() || !send((uint8_t)(address << 1))) {
        result = 5;
    } else if (SSPCON2bits.ACKSTAT) {
        result = 2;
    } else {
        for (uint8_t i = 0; i < length && !result; i++) {
            if (!send(buffer[i]))
                result = 5;
            else if (SSPCON2bits.ACKSTAT)
                result = 3;
        }
    }
    if (stop || result)
        stop_bus();
    length = index = 0;
    return result;
}

uint8_t TwoWire::requestFrom(uint8_t from, uint8_t quantity, bool stop)
{
    if (quantity > BUFFER_LENGTH)
        quantity = BUFFER_LENGTH;
    length = index = 0;
    if (SSPSTATbits.S && !SSPSTATbits.P)
        SSPCON2bits.RSEN = 1;
    else
        SSPCON2bits.SEN = 1;
    if (settled() && send((uint8_t)(from << 1 | 1)) && !SSPCON2bits.ACKSTAT) {
        while (length < quantity) {
            SSPCON2bits.RCEN = 1;
            if (!settled())
                break;
            buffer[length++] = SSPBUF;
            // Every byte but the last is acknowledged, to ask for another.
            SSPCON2bits.ACKDT = length == quantity;
            SSPCON2bits.ACKEN = 1;
            if (!settled())
                break;
        }
        if (stop)
            stop_bus();
    } else {
        stop_bus();
    }
    return length;
}
