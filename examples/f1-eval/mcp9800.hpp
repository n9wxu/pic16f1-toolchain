// The MCP9800 temperature sensor, on any I2C bus.
//
// It is kept shut down and asked for one conversion at a time (12 bits,
// a quarter of a second), which is the way to use it from a battery: it
// draws a microamp between conversions in place of 200.
#ifndef F1_EVAL_MCP9800_HPP
#define F1_EVAL_MCP9800_HPP

#include <compare>
#include "i2c.hpp"

// A temperature as the sensor gives it: sixteenths of a degree Celsius.
// A type of its own, so that a count of sixteenths cannot be taken for a
// count of tenths; the conversions are the only way out.
struct Celsius {
    int16_t sixteenths;

    constexpr int16_t tenths() const { return int16_t(sixteenths * 5 / 8); }
    constexpr int16_t fahrenheit_tenths() const { return int16_t(sixteenths * 9 / 8 + 320); }
    constexpr auto operator<=>(const Celsius &) const = default;
};
// 21.5_degC: for limits and for tests, converted by the compiler.
consteval Celsius operator""_degC(long double degrees)
{
    return {int16_t(degrees * 16)};
}
consteval Celsius operator""_degC(unsigned long long degrees)
{
    return {int16_t(degrees * 16)};
}

template <i2c::Bus Bus> class Mcp9800 {
    enum Register : uint8_t { Temperature, Configuration };
    enum : uint8_t { Shutdown = 0x01, Bits12 = 0x60, OneShot = 0x80 };

    // The address is 0x48 to 0x4F by the part number's suffix; begin()
    // finds which.
    static inline uint8_t address;

public:
    // The two bytes of the temperature register: twelve bits, signed,
    // at the top.
    static constexpr Celsius decode(uint8_t high, uint8_t low)
    {
        return {int16_t(int16_t(uint16_t(high) << 8 | low) >> 4)};
    }

    // Finds the sensor and shuts it down. The address it answered at, or
    // why none did.
    [[nodiscard]] static i2c::Result<uint8_t> begin()
    {
        const uint8_t setup[] = {Configuration, Shutdown | Bits12};
        i2c::Result<> tried;
        for (address = 0x48; address <= 0x4F; address++) {
            tried = Bus::write(address, setup);
            if (tried)
                return address;
            if (tried.error() != i2c::Error::NoDevice)
                break;
        }
        address = 0;
        return std::unexpected(tried.error());
    }
    static bool found() { return address != 0; }

    // Starts a conversion; its result can be read 240 ms later.
    [[nodiscard]] static i2c::Result<> convert()
    {
        const uint8_t once[] = {Configuration, Shutdown | Bits12 | OneShot};
        return Bus::write(address, once);
    }

    // The result of the last conversion.
    [[nodiscard]] static i2c::Result<Celsius> read()
    {
        const uint8_t pointer[] = {Temperature};
        uint8_t raw[2];
        if (auto sent = Bus::write(address, pointer); !sent)
            return std::unexpected(sent.error());
        if (auto got = Bus::read(address, raw); !got)
            return std::unexpected(got.error());
        return decode(raw[0], raw[1]);
    }
};

// The arithmetic is checked where it is written: these run in the
// compiler, on every build, and cost the part nothing.
namespace mcp9800_tests {
struct NoBus {
    static i2c::Result<> write(uint8_t, std::span<const uint8_t>);
    static i2c::Result<> read(uint8_t, std::span<uint8_t>);
};
using Sensor = Mcp9800<NoBus>;
static_assert(Sensor::decode(0x19, 0x10) == Celsius{25 * 16 + 1}); // 25.0625, from the data sheet
static_assert(Sensor::decode(0x19, 0x80) == 25.5_degC);
static_assert(Sensor::decode(0xE7, 0x00) == Celsius{-25 * 16});
static_assert((25.5_degC).tenths() == 255);
static_assert((0_degC).fahrenheit_tenths() == 320);
static_assert((100_degC).fahrenheit_tenths() == 2120);
static_assert(Celsius{-40 * 16}.fahrenheit_tenths() == -400);
static_assert(20_degC < 25.5_degC);
} // namespace mcp9800_tests

#endif
