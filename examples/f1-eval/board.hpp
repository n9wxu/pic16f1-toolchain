// The F1 Evaluation Platform (DM164130-1): what is wired to which pin of
// its PIC16LF1937, from table 3-1 of the board's user's guide (DS41401).
// Everything here is a name for a type; none of it is code or RAM.
#ifndef F1_EVAL_BOARD_HPP
#define F1_EVAL_BOARD_HPP

// The internal oscillator, as main() sets it. Before the headers: the
// delays, the serial port's and the I2C bus's rates are computed from it.
#define _XTAL_FREQ 4000000UL
#include <pic/pic.hpp>

#include "button.hpp"
#include "i2c.hpp"
#include "lcd.hpp"
#include "mcp9800.hpp"
#include "rtc.hpp"

namespace board {

// ---- Time --------------------------------------------------------------------
// The crystal wakes the processor 32 times a second; everything is timed
// in those ticks.
using Clock = Rtc<32>;

// 250_ms is a count of ticks, worked out by the compiler; a time that is
// not a whole number of ticks, or too long for a byte, does not compile.
void not_a_whole_number_of_ticks();
consteval uint8_t operator""_ms(unsigned long long ms)
{
    unsigned long long ticks = ms * Clock::per_second / 1000;
    if (ticks * 1000 != ms * Clock::per_second || ticks == 0 || ticks > 255)
        not_a_whole_number_of_ticks();
    return uint8_t(ticks);
}

// ---- The LEDs ------------------------------------------------------------------
// D1 to D4, in the order they stand on the board. (D2 to D4 share their
// pins with the motor connector's high-side drives.)
template <typename P>
concept OutputPin = requires {
    P::output();
    P::set();
    P::clear();
};

template <OutputPin... Pins> struct Bar {
    static constexpr uint8_t size = sizeof...(Pins);
    static void begin()
    {
        (Pins::clear(), ...);
        (Pins::output(), ...);
    }
    // The first `lit` of them on, the rest off: one bit instruction each.
    static void level(uint8_t lit)
    {
        uint8_t i = 0;
        (Pins::write(i++ < lit), ...);
    }
};
using Leds = Bar<pic::Pin<'D', 1>, pic::Pin<'E', 2>, pic::Pin<'E', 1>, pic::Pin<'E', 0>>;

// ---- The button, the potentiometer, the sensor -----------------------------------
// S1 pulls RD2 low; a second's hold is a long press.
using Key = Button<pic::Pin<'D', 2>, false, 1000_ms>;

using PotPin = pic::Pin<'B', 2>;
inline constexpr uint8_t pot_channel = 8; // RB2 is AN8

using Bus = i2c::Master<100'000>;
using Sensor = Mcp9800<Bus>;

// ---- The glass -------------------------------------------------------------------
// Three and a half digits and the legends of a multimeter, on four commons
// and ten segment pins. Digit 1 is the rightmost.
using Lcd = lcd::Controller<0, 1, 2, 4, 5, 10, 12, 16, 17, 20>;
// (The same three bytes the board's original firmware writes.)
static_assert(Lcd::enabled == 0x131437);

struct Glass {
    using Digit1 = lcd::Digit<12, 16>;
    using Digit2 = lcd::Digit<5, 10>;
    using Digit3 = lcd::Digit<2, 4>;
    using One = lcd::Segment<2, 1>; // the half digit: a 1 or nothing
    // The decimal point to the right of each digit.
    using Point2 = Digit2::Extra;
    using Point3 = Digit3::Extra;
    using Point4 = lcd::Segment<3, 1>;
    // The legends.
    using Rc = lcd::Segment<0, 0>;
    using Battery = lcd::Segment<1, 0>;
    using Minus = lcd::Segment<2, 0>;
    using Ac = lcd::Segment<3, 0>;
    using DataHold = lcd::Segment<0, 1>;
    using RangeHold = lcd::Segment<1, 1>;
    using S1 = lcd::Segment<0, 17>;
    using S2 = lcd::Segment<1, 17>;
    using Milli = lcd::Segment<2, 17>;
    using Mega = lcd::Segment<3, 17>;
    using Amps = lcd::Segment<0, 20>;
    using Volts = lcd::Segment<1, 20>;
    using Kilo = lcd::Segment<2, 20>;
    using Ohms = lcd::Segment<3, 20>;

    // A number from -1999 to 1999 with 0 to 3 digits after the point,
    // without leading zeros.
    static void number(int16_t value, uint8_t decimals = 0)
    {
        Minus::write(value < 0);
        uint16_t rest = uint16_t(value < 0 ? -value : value);
        uint8_t ones = rest % 10;
        rest /= 10;
        uint8_t tens = rest % 10;
        rest /= 10;
        uint8_t hundreds = rest % 10;
        bool thousand = rest >= 10;
        One::write(thousand);
        Digit3::show(thousand || hundreds || decimals >= 2 ? lcd::digits[hundreds] : lcd::blank);
        Digit2::show(thousand || hundreds || tens || decimals >= 1 ? lcd::digits[tens] : lcd::blank);
        Digit1::show(lcd::digits[ones]);
        points(decimals);
    }

    // Three glyphs: Glass::text(lcd::glyph('E'), lcd::glyph('r'), lcd::glyph('r')).
    static void text(lcd::Glyph left, lcd::Glyph middle, lcd::Glyph right)
    {
        Minus::off();
        One::off();
        Digit3::show(left);
        Digit2::show(middle);
        Digit1::show(right);
        points(0);
    }

private:
    static void points(uint8_t decimals)
    {
        Point2::write(decimals == 1);
        Point3::write(decimals == 2);
        Point4::write(decimals == 3);
    }
};

// ---- The supply --------------------------------------------------------------------
// VDD, in hundredths of a volt, from what the converter reads of the
// 1.024 V reference against it: 1.024 * 1023 / reading. (In two halves,
// so that 16 bits are enough.)
constexpr uint16_t supply_from(uint16_t reading)
{
    constexpr uint16_t half = 52378; // 102.4 * 1023 / 2
    if (reading == 0)
        return 0;
    return uint16_t(2 * (half / reading) + (2 * (half % reading) >= reading));
}
static_assert(supply_from(327) == 320);
static_assert(supply_from(1023) == 102);

inline uint16_t supply()
{
    pic::Adc::fixed_reference_on(1);
    uint16_t reading = pic::Adc::read(pic::Adc::fixed_reference);
    FVRCONbits.FVREN = 0; // it draws current
    return supply_from(reading);
}

// ---- Pins of the motor connector, which this program does not use -------------------
// The three low-side drives, held low; the rest are analog inputs out of
// reset, and stay so.
inline void park_unused_pins()
{
    LATCbits.LATC2 = 0;
    TRISCbits.TRISC2 = 0;
    pic::Pin<'D', 5>::clear();
    pic::Pin<'D', 5>::output();
    pic::Pin<'D', 6>::clear();
    pic::Pin<'D', 6>::output();
}

} // namespace board

#endif
