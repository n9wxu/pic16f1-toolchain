// Everything on the F1 Evaluation Platform at once, in C++23, on a
// PIC16LF1937 that is asleep nearly all of the time.
//
//   the glass      a clock, the temperature, the potentiometer or the supply
//   the button     short: the next of those four. Long: set the clock
//                  (turn the potentiometer off centre to run the time
//                  forward or back, press to finish), or change between
//                  Celsius and Fahrenheit
//   the pot        read eight times a second; also a bar on the LEDs
//   the LEDs       D1 to D4: how far the potentiometer is turned
//   the sensor     an MCP9800, asked for one conversion each second
//   the crystal    timer 1: the time of day, the tick everything runs on,
//                  and the clock of the LCD while the processor sleeps
//   the EEPROM     what was being shown, and the unit, across power-off
//   the serial pin a line each second (RC6, 9600 baud; pin 6 of the
//                  PICkit Serial header)
//
// board.hpp says what is wired where; the other headers are the drivers.
// See README.md for what of C++ each one shows and what it costs.

#pragma config FOSC = INTOSC, WDTE = OFF, PWRTE = ON, MCLRE = ON
#pragma config BOREN = OFF, CLKOUTEN = OFF, IESO = OFF, FCMEN = OFF
#pragma config WRT = OFF, PLLEN = OFF, STVREN = ON, BORV = LO, LVP = ON

#include <utility>
#include "board.hpp"

namespace {

using namespace board;
using Serial = pic::Uart;

// ---- Text for the serial port ----------------------------------------------------
// `out << now << "  pot " << pot` over anything with write(byte): the
// operators are resolved by the compiler and what is left is the calls.
struct Fixed {
    int16_t value;
    uint8_t decimals; // 1 or 2
};

template <typename Sink> struct Text {
    const Text &operator<<(char ch) const
    {
        Sink::write(uint8_t(ch));
        return *this;
    }
    const Text &operator<<(const char *text) const
    {
        Sink::write(text);
        return *this;
    }
    const Text &operator<<(uint16_t number) const
    {
        Sink::print(number);
        return *this;
    }
    const Text &operator<<(Fixed number) const
    {
        uint16_t rest = uint16_t(number.value);
        if (number.value < 0) {
            *this << '-';
            rest = uint16_t(-number.value);
        }
        uint8_t scale = number.decimals == 2 ? 100 : 10;
        *this << uint16_t(rest / scale) << '.';
        return two(uint8_t(rest % scale), number.decimals == 2);
    }
    const Text &operator<<(const Time &time) const
    {
        return two(time.hour) << ':', two(time.minute) << ':', two(time.second);
    }

private:
    const Text &two(uint8_t number, bool both = true) const
    {
        if (both)
            *this << char('0' + number / 10);
        return *this << char('0' + number % 10);
    }
};
constexpr Text<Serial> out;

// ---- Ticks ---------------------------------------------------------------------------
// The interrupt handler only counts; the work is done by main, awake.
volatile uint8_t pending;

bool take_tick()
{
    pic::CriticalSection quiet;
    if (!pending)
        return false;
    pending = pending - 1;
    return true;
}

// ---- The program ------------------------------------------------------------------------
enum class Mode : uint8_t { Clock, Temperature, Pot, Supply };
constexpr uint8_t modes = 4;
// Where the settings are kept in the EEPROM.
enum : uint8_t { SavedMode, SavedUnit };

class Demo {
    Time now{12, 0, 0};
    Key key;
    Mode mode = Mode::Clock;
    bool setting = false;    // the potentiometer is moving the clock
    bool fahrenheit = false;
    uint8_t phase = 0;       // ticks into the second
    uint8_t title = 0;       // eighths of a second the mode's name still shows
    uint16_t pot = 0;        // 0 to 1023
    uint16_t volts = 0;      // the supply, in hundredths
    i2c::Result<Celsius> temperature = std::unexpected(i2c::Error::NoDevice);

public:
    void begin()
    {
        if (uint8_t saved = pic::Eeprom::read(SavedMode); saved < modes)
            mode = Mode(saved);
        fahrenheit = pic::Eeprom::read(SavedUnit) == 1;

        out << "F1 Evaluation Platform: ";
        if (auto found = Sensor::begin()) {
            out << "MCP9800 at 0x";
            Serial::print_hex(*found);
            (void)Sensor::convert();
        } else {
            temperature = std::unexpected(found.error());
            out << "no temperature sensor";
        }
        out << "\r\n";
        announce();
    }

    // Thirty-two times a second.
    void tick()
    {
        switch (key.poll()) {
        case Press::Short:
            if (setting) {
                setting = false;
            } else {
                mode = Mode((std::to_underlying(mode) + 1) % modes);
                pic::Eeprom::update(SavedMode, std::to_underlying(mode));
                announce();
            }
            break;
        case Press::Long:
            held();
            break;
        case Press::None:
            break;
        }

        if (++phase == Clock::per_second) {
            phase = 0;
            second();
        }
        if (phase % 4 == 0) {
            pot = pic::Adc::read(pot_channel);
            Leds::level(uint8_t((pot + 128) >> 8)); // none to all four
            if (setting)
                turn_clock();
            show();
        }
    }

private:
    void announce() { title = mode == Mode::Clock ? 0 : 6; }

    void held()
    {
        using enum Mode;
        if (mode == Clock) {
            setting = true;
        } else if (mode == Temperature) {
            fahrenheit = !fahrenheit;
            pic::Eeprom::update(SavedUnit, fahrenheit);
            announce();
        }
    }

    // The further the potentiometer is from its middle, the faster the
    // time runs, forward or back: 8 minutes a second to 56.
    void turn_clock()
    {
        int8_t minutes = int8_t((int16_t(pot) - 512) / 64);
        if (minutes) {
            now.add_minutes(minutes);
            now.second = 0;
        }
    }

    void second()
    {
        now.tick();
        if (Sensor::found()) {
            // Last second's conversion, and the start of the next.
            temperature = Sensor::read();
            (void)Sensor::convert();
        }
        volts = supply();

        out << now << "  ";
        if (temperature)
            out << Fixed{temperature->tenths(), 1} << " C";
        else
            out << "no sensor";
        out << "  pot " << pot << "  " << Fixed{int16_t(volts), 2} << " V\r\n";
    }

    void show()
    {
        using enum Mode;
        using lcd::glyph;
        Glass::S1::write(setting);
        Glass::Battery::write(mode == Supply);
        Glass::Volts::write(mode == Supply && !title);

        if (title) {
            title--;
            if (mode == Temperature)
                Glass::text(lcd::blank, lcd::degree, fahrenheit ? glyph('F') : glyph('C'));
            else if (mode == Pot)
                Glass::text(glyph('P'), glyph('o'), glyph('t'));
            else
                Glass::text(glyph('b'), glyph('A'), glyph('t'));
            return;
        }
        switch (mode) {
        case Clock:
            Glass::number(int16_t(now.twelve_hour()), 2);
            // The point between hours and minutes is the colon, and blinks.
            if (!setting && phase >= Clock::per_second / 2)
                Glass::Point3::off();
            break;
        case Temperature:
            if (temperature)
                Glass::number(fahrenheit ? temperature->fahrenheit_tenths() : temperature->tenths(), 1);
            else
                Glass::text(glyph('E'), lcd::blank, lcd::digits[std::to_underlying(temperature.error())]);
            break;
        case Pot:
            Glass::number(int16_t(pot));
            break;
        case Supply:
            Glass::number(int16_t(volts), 2);
            break;
        }
    }
};

constinit Demo demo;

} // namespace

extern "C" void __attribute__((interrupt)) isr()
{
    if (Clock::ticked())
        pending = pending + 1;
}

int main()
{
    pic::set_clock(pic::Clock::MHz4);
    park_unused_pins();
    Leds::begin();
    Key::begin();
    PotPin::input();
    PotPin::analog();
    pic::Adc::begin();
    Serial::begin(9600);
    Clock::begin(); // the crystal first: the LCD runs on it
    Lcd::begin();
    Bus::begin();
    demo.begin();

    pic::enable_interrupts();
    for (;;) {
        while (take_tick())
            demo.tick();
        // Asleep until the crystal's next tick: the oscillator stops, the
        // timer and the LCD go on. (Not when built for a debugger on the
        // part, -mdebugger: a probe cannot reach a processor that sleeps.)
        Serial::flush();
#ifndef __DEBUG
        SLEEP();
#endif
    }
}
