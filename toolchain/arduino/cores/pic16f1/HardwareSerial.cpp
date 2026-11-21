#include "Arduino.h"

static uint8_t buffer[SERIAL_RX_BUFFER_SIZE];
static volatile uint8_t head, tail;

HardwareSerial Serial;

extern "C" void (*__arduino_serial_isr)(void);

// Called by the interrupt handler in wiring.cpp, once begin has said so.
static void received(void)
{
    if (!PIR1bits.RCIF)
        return;
    // An overrun stops the receiver until it is cleared.
    if (RCSTAbits.OERR) {
        RCSTAbits.CREN = 0;
        RCSTAbits.CREN = 1;
    }
    uint8_t c = RCREG;
    uint8_t next = (uint8_t)(head + 1) % SERIAL_RX_BUFFER_SIZE;
    // (A byte that finds the buffer full is dropped.)
    if (next != tail) {
        buffer[head] = c;
        head = next;
    }
}

void HardwareSerial::start(uint16_t divisor)
{
    BAUDCONbits.BRG16 = 1;
    TXSTAbits.BRGH = 1;
    SP1BRGL = (uint8_t)divisor;
    SP1BRGH = (uint8_t)(divisor >> 8);
    TRISCbits.TRISC7 = 1;
    RCSTAbits.SPEN = 1;
    TXSTAbits.TXEN = 1;
    RCSTAbits.CREN = 1;
    __arduino_serial_isr = received;
    PIE1bits.RCIE = 1;
    INTCONbits.PEIE = 1;
}

void HardwareSerial::end()
{
    flush();
    PIE1bits.RCIE = 0;
    RCSTAbits.SPEN = 0;
    head = tail = 0;
}

int HardwareSerial::available()
{
    return (uint8_t)(SERIAL_RX_BUFFER_SIZE + head - tail) % SERIAL_RX_BUFFER_SIZE;
}

int HardwareSerial::peek()
{
    return head == tail ? -1 : buffer[tail];
}

int HardwareSerial::read()
{
    if (head == tail)
        return -1;
    uint8_t c = buffer[tail];
    tail = (uint8_t)(tail + 1) % SERIAL_RX_BUFFER_SIZE;
    return c;
}

int HardwareSerial::availableForWrite()
{
    return PIR1bits.TXIF ? 1 : 0;
}

void HardwareSerial::flush()
{
    while (!TXSTAbits.TRMT) {
    }
}

size_t HardwareSerial::write(uint8_t c)
{
    while (!PIR1bits.TXIF) {
    }
    TXREG = c;
    return 1;
}
