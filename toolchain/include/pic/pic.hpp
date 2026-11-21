// A small C++ library for the PIC16F193x (written for the PIC16F1937):
// pins, delays, the serial port, the A/D converter, the data EEPROM and
// interrupts, as templates and inline functions over the registers of
// <xc.h>. Nothing here takes RAM or code unless it is used, and a pin
// operation is the single instruction it would be in assembly.
//
//     #define _XTAL_FREQ 8000000UL      // before this header, for delays and baud rates
//     #include <pic/pic.hpp>
//
//     using Led = pic::Pin<'E', 0>;
//     Led::output();
//     Led::toggle();
//     pic::delay_ms(500);
//
// Everything is in namespace pic. See docs/languages.md.
#ifndef PIC_PIC_HPP
#define PIC_PIC_HPP

#include <xc.h>
#include <stdint.h>

#if !defined(__PIC16F1937__) && !defined(__PIC16F1032__)
#warning "<pic/pic.hpp> is written for the PIC16F193x; check the register names for this device"
#endif

extern "C" void __eecon_unlock(unsigned int keys);

namespace pic {

// ---- Registers by port letter ----------------------------------------------
// The registers of a port, by letter. (The device header gives them as
// PORTA, TRISA, LATA, ANSELA; this gives them to templates.)
template <char Letter> struct Port;
#define PIC_PORT(L, P)                                                         \
    template <> struct Port<L> {                                               \
        static volatile uint8_t &port() { return PORT##P; }                    \
        static volatile uint8_t &tris() { return TRIS##P; }                    \
        static volatile uint8_t &lat() { return LAT##P; }                      \
        static volatile uint8_t &ansel() { return ANSEL##P; }                  \
    };
#ifdef PORTA
PIC_PORT('A', A)
#endif
#ifdef PORTB
PIC_PORT('B', B)
#endif
#ifdef PORTD
PIC_PORT('D', D)
#endif
#ifdef PORTE
PIC_PORT('E', E)
#endif
#undef PIC_PORT
// Port C has no analog inputs on the PIC16F193x, so no ANSELC.
#ifdef PORTC
template <> struct Port<'C'> {
    static volatile uint8_t &port() { return PORTC; }
    static volatile uint8_t &tris() { return TRISC; }
    static volatile uint8_t &lat() { return LATC; }
};
#endif

// ---- Pins -------------------------------------------------------------------
// One pin, known at compile time: Pin<'E', 0> is RE0. Every operation is
// one bit instruction on the register.
template <char Letter, uint8_t Bit> struct Pin {
    static_assert(Bit < 8, "a port has eight pins");
    static constexpr uint8_t mask = uint8_t(1u << Bit);
    using P = Port<Letter>;

    static void output() { P::tris() &= uint8_t(~mask); }
    static void input() { P::tris() |= mask; }
    // A pin with an analog function reads 0 until it is made digital.
    static void digital() { P::ansel() &= uint8_t(~mask); }
    static void analog() { P::ansel() |= mask; }
    static void set() { P::lat() |= mask; }
    static void clear() { P::lat() &= uint8_t(~mask); }
    static void toggle() { P::lat() ^= mask; }
    static void write(bool on)
    {
        if (on)
            set();
        else
            clear();
    }
    static bool read() { return (P::port() & mask) != 0; }
    // What the pin is driven to, as opposed to what it reads.
    static bool driven() { return (P::lat() & mask) != 0; }
};

// ---- Interrupts ---------------------------------------------------------------
inline void enable_interrupts() { INTCONbits.GIE = 1; }
inline void disable_interrupts() { INTCONbits.GIE = 0; }
// Interrupts off for the life of the object, and back as they were after.
class CriticalSection {
    bool was;

public:
    CriticalSection() : was(INTCONbits.GIE) { INTCONbits.GIE = 0; }
    ~CriticalSection()
    {
        if (was)
            INTCONbits.GIE = 1;
    }
    CriticalSection(const CriticalSection &) = delete;
    CriticalSection &operator=(const CriticalSection &) = delete;
};

// ---- The internal oscillator --------------------------------------------------
enum class Clock : uint8_t {
    Hz500k = 0x38, // as out of reset
    MHz1 = 0x58,
    MHz2 = 0x60,
    MHz4 = 0x68,
    MHz8 = 0x70,
    MHz16 = 0x78,
};
inline void set_clock(Clock c) { OSCCON = uint8_t(c); }

#ifdef _XTAL_FREQ
// ---- Delays ---------------------------------------------------------------------
// At least the time asked for, and up to a dozen instruction cycles more;
// interrupts lengthen it. With a constant argument the arithmetic is done
// by the compiler.
inline void delay_cycles(unsigned long cycles)
{
    __delay_loops(cycles > 16UL ? (cycles - 7UL) / 10UL : 0UL);
}
inline void delay_us(unsigned long us) { delay_cycles(us * (_XTAL_FREQ / 4000UL) / 1000UL); }
inline void delay_ms(unsigned int ms) { delay_cycles(ms * (_XTAL_FREQ / 4000UL)); }

// ---- The serial port (EUSART, pins RC6 = TX and RC7 = RX) -------------------------
struct Uart {
    // 8 data bits, no parity, one stop bit.
    static void begin(unsigned long baud)
    {
        unsigned int divisor = (unsigned int)((_XTAL_FREQ / 4UL + baud / 2UL) / baud - 1UL);
        BAUDCONbits.BRG16 = 1;
        TXSTAbits.BRGH = 1;
        SP1BRGL = uint8_t(divisor);
        SP1BRGH = uint8_t(divisor >> 8);
        TRISCbits.TRISC7 = 1;
        RCSTAbits.SPEN = 1;
        TXSTAbits.TXEN = 1;
        RCSTAbits.CREN = 1;
    }
    static bool can_write() { return PIR1bits.TXIF; }
    static void write(uint8_t byte)
    {
        while (!PIR1bits.TXIF) {
        }
        TXREG = byte;
    }
    static void write(const char *text)
    {
        while (*text)
            write(uint8_t(*text++));
    }
    // Until the last byte has left the pin.
    static void flush()
    {
        while (!TXSTAbits.TRMT) {
        }
    }
    static bool available() { return PIR1bits.RCIF; }
    // Waits for a byte. An overrun, which stops the receiver, is cleared.
    static uint8_t read()
    {
        if (RCSTAbits.OERR) {
            RCSTAbits.CREN = 0;
            RCSTAbits.CREN = 1;
        }
        while (!PIR1bits.RCIF) {
        }
        return RCREG;
    }
    // A number in decimal, without the C library.
    static void print(unsigned int value)
    {
        char digits[5];
        uint8_t n = 0;
        do {
            digits[n++] = char('0' + value % 10);
            value /= 10;
        } while (value);
        while (n)
            write(uint8_t(digits[--n]));
    }
    static void print_hex(uint8_t value)
    {
        static const char hex[] = "0123456789ABCDEF";
        write(uint8_t(hex[value >> 4]));
        write(uint8_t(hex[value & 15]));
    }
};
#endif // _XTAL_FREQ

// ---- The A/D converter (10 bits) ----------------------------------------------------
struct Adc {
    static constexpr uint8_t temperature = 29, dac = 30, fixed_reference = 31;
    // Right-justified results, VDD and VSS as references. The clock is
    // the oscillator divided down to 2 to 4 us a bit, if _XTAL_FREQ says
    // it is 8 MHz or less; otherwise the converter's own RC clock, which
    // runs at any oscillator setting and in sleep, but with which the
    // PIC16F193x now and then loses a conversion when it is not asleep
    // (see read).
    static void begin()
    {
#if defined(_XTAL_FREQ) && _XTAL_FREQ <= 8000000UL
        ADCON1 = 0x80 | (_XTAL_FREQ <= 1000000UL   ? 0x00   // Fosc/2
                         : _XTAL_FREQ <= 2000000UL ? 0x40   // Fosc/4
                         : _XTAL_FREQ <= 4000000UL ? 0x10   // Fosc/8
                                                   : 0x50); // Fosc/16
#else
        ADCON1 = 0xF0;
#endif
        ADCON0bits.ADON = 1;
    }
    // The fixed voltage reference as an input to measure against: 1 gives
    // 1.024 V, 2 gives 2.048 V, 3 gives 4.096 V. Waits until it is stable.
    static void fixed_reference_on(uint8_t gain)
    {
        FVRCONbits.ADFVR = gain;
        FVRCONbits.FVREN = 1;
        while (!FVRCONbits.FVRRDY) {
        }
    }
    // Channel 0-13 is pin AN0-AN13 (which must be an analog input).
    static uint16_t read(uint8_t channel)
    {
        ADCON0bits.CHS = channel;
        // The sampling capacitor needs about 5 us to follow the new input.
        for (volatile uint8_t settle = 8; settle; settle = settle - 1) {
        }
        // A conversion takes at most 200 instruction cycles. One that has
        // not finished in several times that never will: the PIC16F193x
        // can lose one (errata DS80000479: with the converter's own RC
        // clock when not asleep, or above 8 MHz). It is stopped and
        // started again.
        for (;;) {
            ADCON0bits.GO_nDONE = 1;
            for (uint8_t patience = 255; patience; patience--)
                if (!ADCON0bits.GO_nDONE)
                    return ADRES;
            ADCON0bits.GO_nDONE = 0;
        }
    }
};

// ---- The data EEPROM (256 bytes) ------------------------------------------------------
struct Eeprom {
    static uint8_t read(uint8_t address)
    {
        EEADRL = address;
        EECON1bits.EEPGD = 0;
        EECON1bits.CFGS = 0;
        EECON1bits.RD = 1;
        return EEDATL;
    }
    // Takes about 4 ms; waits for it. Interrupts are off for the
    // instructions that unlock the write.
    //
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
    static void write(uint8_t address, uint8_t value)
    {
        keys = 0xAA55;
        EEADRL = address;
        EEDATL = value;
        EECON1bits.EEPGD = 0;
        EECON1bits.CFGS = 0;
        EECON1bits.WREN = 1;
        {
            CriticalSection quiet;
            __eecon_unlock(keys);
        }
        keys = 0;
        while (EECON1bits.WR) {
        }
        EECON1bits.WREN = 0;
    }
    // Writes only if the byte differs: an EEPROM cell wears out.
    static void update(uint8_t address, uint8_t value)
    {
        if (read(address) != value)
            write(address, value);
    }
};

// ---- Timer 0 as a free-running counter ---------------------------------------------------
struct Timer0 {
    // Counts instruction cycles divided by 2^(shift + 1), shift 0-7; with
    // interrupts on, the handler is entered each time it wraps (256 counts).
    static void begin(uint8_t shift, bool interrupts = false)
    {
        OPTION_REG = uint8_t((OPTION_REG & 0xC0) | (shift & 7));
        TMR0 = 0;
        INTCONbits.TMR0IF = 0;
        INTCONbits.TMR0IE = interrupts;
    }
    static uint8_t count() { return TMR0; }
    // True once for each wrap, for a handler or a polling loop.
    static bool wrapped()
    {
        if (!INTCONbits.TMR0IF)
            return false;
        INTCONbits.TMR0IF = 0;
        return true;
    }
};

} // namespace pic

#endif
