// The MSSP as an I2C master (SCL on RC3, SDA on RC4), polled.
//
// Nothing here can hang: every wait is bounded, and what went wrong comes
// back as a value (std::expected), since there are no exceptions on a
// part this size. A Transaction sends the stop condition when it goes out
// of scope, whichever way the function is left.
#ifndef F1_EVAL_I2C_HPP
#define F1_EVAL_I2C_HPP

#include <expected>
#include <span>
#include <pic/pic.hpp>

namespace i2c {

enum class Error : uint8_t {
    Timeout = 1, // the bus did not finish: held low, or no pull-ups
    NoDevice,    // nothing acknowledged the address
    Refused,     // the device did not acknowledge a byte
};
template <typename T = void> using Result = std::expected<T, Error>;

// What a driver needs of a bus, so that a driver is a template over any
// bus that has it (this one, or one made of two pins, or a test's).
template <typename B>
concept Bus = requires(uint8_t address, std::span<const uint8_t> out, std::span<uint8_t> in) {
    { B::write(address, out) } -> std::same_as<Result<>>;
    { B::read(address, in) } -> std::same_as<Result<>>;
};

template <unsigned long Hz> struct Master {
    // The bit rate is Fosc / (4 * (SSPADD + 1)); the compiler does the
    // division and refuses a rate the oscillator cannot give.
    static constexpr unsigned long divisor = _XTAL_FREQ / (4UL * Hz) - 1UL;
    static_assert(divisor >= 3 && divisor <= 255, "this bit rate is out of reach at _XTAL_FREQ");

    using Scl = pic::Pin<'C', 3>;
    using Sda = pic::Pin<'C', 4>;

    static void begin()
    {
        SSPCON1 = 0;
        Scl::input();
        Sda::input();
        // A device that was cut off in the middle of a byte (by a reset of
        // this part, say) holds the data line low until it has been
        // clocked through the rest of it.
        Scl::clear();
        for (uint8_t i = 0; i < 9 && !Sda::read(); i++) {
            Scl::output();
            pic::delay_us(5);
            Scl::input();
            pic::delay_us(5);
        }
        SSPADD = uint8_t(divisor);
        SSPSTAT = 0x80; // standard-speed edges
        SSPCON2 = 0;
        SSPCON1 = 0x28; // on, I2C master
    }

    // Address is the 7-bit address.
    [[nodiscard]] static Result<> write(uint8_t address, std::span<const uint8_t> bytes)
    {
        Transaction bus;
        if (auto began = bus.begin(uint8_t(address << 1)); !began)
            return began;
        for (uint8_t byte : bytes) {
            if (!bus.send(byte))
                return std::unexpected(Error::Timeout);
            if (SSPCON2bits.ACKSTAT)
                return std::unexpected(Error::Refused);
        }
        return {};
    }

    [[nodiscard]] static Result<> read(uint8_t address, std::span<uint8_t> bytes)
    {
        Transaction bus;
        if (auto began = bus.begin(uint8_t(address << 1 | 1)); !began)
            return began;
        for (uint8_t i = 0; i < bytes.size(); i++) {
            SSPCON2bits.RCEN = 1;
            if (!settled())
                return std::unexpected(Error::Timeout);
            bytes[i] = SSPBUF;
            // Every byte but the last is acknowledged, to ask for another.
            SSPCON2bits.ACKDT = i + 1 == bytes.size();
            SSPCON2bits.ACKEN = 1;
            if (!settled())
                return std::unexpected(Error::Timeout);
        }
        return {};
    }

private:
    // Until the module has finished what it was told to do, or 2 ms.
    [[nodiscard]] static bool settled()
    {
        for (uint8_t patience = 255; patience; patience--)
            if (!(SSPCON2 & 0x1F) && !SSPSTATbits.R_nW)
                return true;
        return false;
    }

    struct Transaction {
        Transaction() = default;
        Transaction(const Transaction &) = delete;
        ~Transaction()
        {
            SSPCON2bits.PEN = 1;
            (void)settled();
        }
        [[nodiscard]] Result<> begin(uint8_t address_byte)
        {
            SSPCON2bits.SEN = 1;
            if (!settled() || !send(address_byte))
                return std::unexpected(Error::Timeout);
            if (SSPCON2bits.ACKSTAT)
                return std::unexpected(Error::NoDevice);
            return {};
        }
        [[nodiscard]] bool send(uint8_t byte)
        {
            SSPBUF = byte;
            return settled();
        }
    };
};

} // namespace i2c

#endif
