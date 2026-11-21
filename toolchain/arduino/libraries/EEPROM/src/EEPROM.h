// EEPROM: the 256 bytes of data EEPROM. A write takes about 4 ms and is
// waited for; update and put write only the bytes that change, since a
// cell wears out.
#ifndef EEPROM_h
#define EEPROM_h

#include <Arduino.h>

#ifndef E2END
#define E2END 0xFF
#endif

extern "C" void __eecon_unlock(unsigned int keys);

class EEPROMClass {
    // EEPROM[i] reads; EEPROM[i] = x writes.
    struct Cell {
        uint8_t address;
        operator uint8_t() const { return EEPROMClass::read(address); }
        Cell &operator=(uint8_t value)
        {
            EEPROMClass::update(address, value);
            return *this;
        }
        Cell &operator=(const Cell &other) { return *this = (uint8_t)other; }
    };

public:
    static uint8_t read(int address)
    {
        EEADRL = (uint8_t)address;
        EECON1bits.EEPGD = 0;
        EECON1bits.CFGS = 0;
        EECON1bits.RD = 1;
        return EEDATL;
    }
    // The two values that unlock a write are never loaded as constants where
    // EECON2 is written: a program that has lost its way and lands there
    // would then write. They are put in two variables first (arming the
    // write), the unlock copies them from there, and they are cleared
    // after; code reached by accident finds zeros, and nothing is written.
    //
    // The unlock itself is five instructions that the part wants with
    // nothing between them; they are in the runtime library
    // (__eecon_unlock), which is given the two values.
    static inline volatile uint16_t keys;
    static void write(int address, uint8_t value)
    {
        keys = 0xAA55;
        EEADRL = (uint8_t)address;
        EEDATL = value;
        EECON1bits.EEPGD = 0;
        EECON1bits.CFGS = 0;
        EECON1bits.WREN = 1;
        // Interrupts are off for the instructions that unlock the write.
        uint8_t was = INTCON;
        INTCONbits.GIE = 0;
        __eecon_unlock(keys);
        if (was & 0x80)
            INTCONbits.GIE = 1;
        keys = 0;
        while (EECON1bits.WR) {
        }
        EECON1bits.WREN = 0;
    }
    static void update(int address, uint8_t value)
    {
        if (read(address) != value)
            write(address, value);
    }
    static constexpr uint16_t length() { return E2END + 1; }
    Cell operator[](int address) { return Cell{(uint8_t)address}; }

    template <typename T> T &get(int address, T &value)
    {
        uint8_t *p = (uint8_t *)&value;
        for (uint8_t i = 0; i < sizeof(T); i++)
            p[i] = read(address + i);
        return value;
    }
    template <typename T> const T &put(int address, const T &value)
    {
        const uint8_t *p = (const uint8_t *)&value;
        for (uint8_t i = 0; i < sizeof(T); i++)
            update(address + i, p[i]);
        return value;
    }
};

inline EEPROMClass EEPROM;

#endif
