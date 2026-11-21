// Pins whose number is known only when the program runs. (A constant pin
// never gets here: see Arduino.h.)
#include "Arduino.h"

static const uint8_t bits[8] = {1, 2, 4, 8, 16, 32, 64, 128};

extern "C" {
void _pinMode(uint8_t pin, uint8_t mode)
{
    uint8_t port = digitalPinToPort(pin);
    uint8_t mask = bits[pin & 7];
    if (port != 2)
        *(&ANSELA + port) &= (uint8_t)~mask;
    if (mode == OUTPUT) {
        *portModeRegister(port) &= (uint8_t)~mask;
        return;
    }
    *portModeRegister(port) |= mask;
    if (port == 1) {
        if (mode == INPUT_PULLUP)
            WPUB |= mask;
        else
            WPUB &= (uint8_t)~mask;
    }
}

void _digitalWrite(uint8_t pin, uint8_t value)
{
    volatile uint8_t *latch = portOutputRegister(digitalPinToPort(pin));
    uint8_t mask = bits[pin & 7];
    if (value)
        *latch |= mask;
    else
        *latch &= (uint8_t)~mask;
}

uint8_t _digitalRead(uint8_t pin)
{
    return (*portInputRegister(digitalPinToPort(pin)) & bits[pin & 7]) != 0;
}
}
