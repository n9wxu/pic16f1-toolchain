// attachInterrupt: the interrupt-on-change of port B, any pin, either
// edge or both. The interrupt number is the pin's bit (see
// digitalPinToInterrupt).
#include "Arduino.h"

static void (*handlers[8])(void);

extern "C" {
extern void (*__arduino_pin_isr)(void);
static void changed(void);
void attachInterrupt(uint8_t interrupt, void (*handler)(void), uint8_t mode)
{
    if (interrupt > 7)
        return;
    uint8_t mask = digitalPinToBitMask(interrupt);
    handlers[interrupt] = handler;
    if (mode != FALLING)
        IOCBP |= mask;
    if (mode != RISING)
        IOCBN |= mask;
    IOCBF &= (uint8_t)~mask;
    __arduino_pin_isr = changed;
    INTCONbits.IOCIE = 1;
}

void detachInterrupt(uint8_t interrupt)
{
    if (interrupt > 7)
        return;
    uint8_t mask = (uint8_t)~digitalPinToBitMask(interrupt);
    IOCBP &= mask;
    IOCBN &= mask;
}

// Called by the interrupt handler in wiring.cpp, once a handler is attached.
static void changed(void)
{
    uint8_t flags = IOCBF;
    if (!flags)
        return;
    // Only the flags seen are cleared: an edge that comes meanwhile keeps
    // its own.
    IOCBF &= (uint8_t)~flags;
    for (uint8_t i = 0; flags; i++, flags >>= 1)
        if ((flags & 1) && handlers[i])
            handlers[i]();
}
}
