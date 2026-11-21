// Timer 1 on the 32.768 kHz crystal: the time of day, and a tick that
// wakes the processor from sleep.
#ifndef F1_EVAL_RTC_HPP
#define F1_EVAL_RTC_HPP

#include <bit>
#include <pic/pic.hpp>

// A time of day. Three bytes; no arithmetic wider than a byte.
struct Time {
    uint8_t hour, minute, second;

    constexpr void tick()
    {
        if (++second < 60)
            return;
        second = 0;
        add_minutes(1);
    }
    // Forward or back by up to two hours, around midnight either way.
    constexpr void add_minutes(int8_t minutes)
    {
        int16_t of_day = int16_t(hour * 60 + minute + minutes);
        if (of_day < 0)
            of_day += 24 * 60;
        if (of_day >= 24 * 60)
            of_day -= 24 * 60;
        hour = uint8_t(of_day / 60);
        minute = uint8_t(of_day % 60);
    }
    // As a clock with twelve hours shows it: 1200 for noon and midnight.
    constexpr uint16_t twelve_hour() const
    {
        uint8_t h = hour % 12;
        return uint16_t((h ? h : 12) * 100 + minute);
    }
    constexpr bool operator==(const Time &) const = default;
};

namespace time_tests {
constexpr Time after(Time t, int8_t minutes)
{
    t.add_minutes(minutes);
    return t;
}
constexpr Time next(Time t)
{
    t.tick();
    return t;
}
static_assert(next({23, 59, 59}) == Time{0, 0, 0});
static_assert(next({9, 5, 58}) == Time{9, 5, 59});
static_assert(after({0, 5, 0}, -10) == Time{23, 55, 0});
static_assert(after({23, 30, 0}, 45) == Time{0, 15, 0});
static_assert(Time{0, 7, 0}.twelve_hour() == 1207);
static_assert(Time{13, 45, 0}.twelve_hour() == 145);
} // namespace time_tests

// The timer, interrupting PerSecond times a second (a power of two, 1 to
// 128). The crystal keeps running in sleep, and so does this.
template <uint8_t PerSecond> struct Rtc {
    static_assert(std::has_single_bit(PerSecond) && PerSecond <= 128,
                  "32,768 counts divide into ticks only by a power of two, and by whole high bytes");
    static constexpr uint8_t per_second = PerSecond;
    // The timer counts up and interrupts as it wraps, so each tick starts
    // it this far from the top. Only the high byte is touched, and by an
    // OR: the low byte counts on undisturbed and no count is ever lost,
    // which is what makes this a clock.
    static constexpr uint8_t reload = uint8_t((0x10000UL - 32768UL / PerSecond) >> 8);

    static void begin()
    {
        T1GCON = 0;
        TMR1L = 0;
        TMR1H = reload;
        // The crystal's oscillator on; count it, not synchronized to the
        // processor's clock (which stops in sleep); timer on.
        T1CON = 0x80 | 0x08 | 0x04 | 0x01;
        PIR1bits.TMR1IF = 0;
        PIE1bits.TMR1IE = 1;
        INTCONbits.PEIE = 1;
    }

    // For the interrupt handler: true if this timer is why it was entered.
    static bool ticked()
    {
        if (!PIR1bits.TMR1IF)
            return false;
        PIR1bits.TMR1IF = 0;
        TMR1H |= reload;
        return true;
    }
};

#endif
