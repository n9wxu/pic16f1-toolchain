// The LCD controller of the PIC16F193x driving a segment glass.
//
// A segment of the glass is a type, as a pin is in <pic/pic.hpp>: which
// common and which segment pin it is on are template arguments, so turning
// it on or off is one bit instruction on one of the LCDDATA registers. A
// seven-segment digit is two segment pins and four commons; showing a
// glyph on one is seven such instructions, each behind a bit test.
#ifndef F1_EVAL_LCD_HPP
#define F1_EVAL_LCD_HPP

#include <array>
#include <pic/pic.hpp>

namespace lcd {

// ---- What a digit shows ------------------------------------------------------
// One bit per segment, in the usual order:
//        a
//      f   b
//        g
//      e   c
//        d
using Glyph = uint8_t;
enum : Glyph { a = 1, b = 2, c = 4, d = 8, e = 16, f = 32, g = 64 };

inline constexpr Glyph blank = 0;
inline constexpr Glyph dash = g;
inline constexpr Glyph degree = a | b | f | g;

// The glyph of a character, for text known when the program is compiled:
// consteval, so it is never code or a table in the part, and a character
// there is no glyph for does not compile.
void no_glyph_for_this_character();
consteval Glyph glyph(char ch)
{
    switch (ch) {
    case ' ': return blank;
    case '-': return dash;
    case '0': case 'O': return a | b | c | d | e | f;
    case '1': case 'I': return b | c;
    case '2': return a | b | d | e | g;
    case '3': return a | b | c | d | g;
    case '4': return b | c | f | g;
    case '5': case 'S': return a | c | d | f | g;
    case '6': return a | c | d | e | f | g;
    case '7': return a | b | c;
    case '8': return a | b | c | d | e | f | g;
    case '9': return a | b | c | d | f | g;
    case 'A': return a | b | c | e | f | g;
    case 'b': return c | d | e | f | g;
    case 'C': return a | d | e | f;
    case 'c': return d | e | g;
    case 'd': return b | c | d | e | g;
    case 'E': return a | d | e | f | g;
    case 'F': return a | e | f | g;
    case 'H': return b | c | e | f | g;
    case 'L': return d | e | f;
    case 'n': return c | e | g;
    case 'o': return c | d | e | g;
    case 'P': return a | b | e | f | g;
    case 'r': return e | g;
    case 't': return d | e | f | g;
    case 'U': return b | c | d | e | f;
    }
    no_glyph_for_this_character();
    return blank;
}

// The ten digits, for numbers known only when the program runs: ten words
// of flash, made by the compiler from the function above.
inline constexpr std::array<Glyph, 10> digits = [] {
    std::array<Glyph, 10> table{};
    for (uint8_t i = 0; i < 10; i++)
        table[i] = glyph(char('0' + i));
    return table;
}();

// ---- One segment ---------------------------------------------------------------
// The segment where common Com (0-3) crosses segment pin Seg (0-23) of the
// controller. Its bit is bit Seg % 8 of LCDDATA(3 * Com + Seg / 8).
template <uint8_t Com, uint8_t Seg> struct Segment {
    static_assert(Com < 4 && Seg < 24, "the controller has 4 commons and 24 segment pins");
    static constexpr uint8_t mask = uint8_t(1u << (Seg % 8));
    static volatile uint8_t &data() { return *(&LCDDATA0 + (Com * 3 + Seg / 8)); }

    static void on() { data() |= mask; }
    static void off() { data() &= uint8_t(~mask); }
    static void write(bool lit)
    {
        if (lit)
            on();
        else
            off();
    }
};

// ---- A seven-segment digit --------------------------------------------------------
// The usual wiring of such glass: one segment pin has a, f, e and d on the
// four commons, another has b, g, c and a fourth thing (a decimal point,
// often), which is Extra here and not part of the digit.
template <uint8_t PinAFED, uint8_t PinBGC> struct Digit {
    using Extra = Segment<3, PinBGC>;

    static void show(Glyph glyph)
    {
        Segment<0, PinAFED>::write(glyph & a);
        Segment<0, PinBGC>::write(glyph & b);
        Segment<2, PinBGC>::write(glyph & c);
        Segment<3, PinAFED>::write(glyph & d);
        Segment<2, PinAFED>::write(glyph & e);
        Segment<1, PinAFED>::write(glyph & f);
        Segment<1, PinBGC>::write(glyph & g);
    }
};

// ---- The controller ------------------------------------------------------------------
// Four commons (1/4 multiplex, 1/3 bias), clocked by the timer 1 oscillator
// so that the glass stays on while the processor sleeps, with the internal
// contrast ladder. Pins lists the segment pins the glass is wired to; the
// three enable registers are worked out from it by the compiler.
template <uint8_t... Pins> struct Controller {
    static constexpr uint32_t enabled = ((uint32_t(1) << Pins) | ...);
    static_assert(((Pins < 24) && ...), "segment pins are 0 to 23");

    // (The timer 1 oscillator must be running: see Rtc::begin.)
    static void begin()
    {
        LCDCON = 0;
        clear();
        LCDSE0 = uint8_t(enabled);
        LCDSE1 = uint8_t(enabled >> 8);
        LCDSE2 = uint8_t(enabled >> 16);
        // Type-A waveform, which lets the data registers be written at any
        // time, and no prescaler: 1,024 Hz over four commons.
        LCDPS = 0x00;
        // The internal ladder from VDD, at full strength, greatest contrast.
        LCDREF = 0x80;
        LCDRL = 0x30;
        LCDCST = 0;
        // On, also in sleep; timer 1 oscillator; four commons.
        LCDCON = 0x80 | 0x04 | 0x03;
    }
    static void clear()
    {
        for (uint8_t i = 0; i < 12; i++)
            *(&LCDDATA0 + i) = 0;
    }
};

} // namespace lcd

#endif
