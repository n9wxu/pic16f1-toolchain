// A push button on a pin, sampled on a regular tick: debounced, and told
// apart into short presses and long ones. One byte of RAM.
#ifndef F1_EVAL_BUTTON_HPP
#define F1_EVAL_BUTTON_HPP

#include <concepts>
#include <pic/pic.hpp>

// Anything that can be read as a pin (a pic::Pin, or a test's stand-in).
template <typename P>
concept InputPin = requires {
    P::input();
    { P::read() } -> std::convertible_to<bool>;
};

enum class Press : uint8_t { None, Short, Long };

// Pin reads Down when the button is pressed. A press held for LongTicks
// samples is reported as Long, once, while it is still held; a shorter
// one is reported as Short when it is let go. A press seen in only one
// sample is contact bounce or noise, and nothing.
template <InputPin Pin, bool Down, uint8_t LongTicks> class Button {
    static_assert(LongTicks > 2 && LongTicks < 255);
    uint8_t held = 0;

public:
    static void begin()
    {
        Pin::digital();
        Pin::input();
    }
    constexpr Press poll() { return sample(Pin::read() == Down); }

    constexpr Press sample(bool down)
    {
        if (down) {
            if (held < 255)
                held++;
            return held == LongTicks ? Press::Long : Press::None;
        }
        uint8_t was = held;
        held = 0;
        return was >= 2 && was < LongTicks ? Press::Short : Press::None;
    }
};

namespace button_tests {
struct NoPin {
    static void input();
    static void digital();
    static bool read();
};
// What a button reports over a run of samples ('#' pressed, '.' not).
template <unsigned N> constexpr Press last_of(const char (&samples)[N], Press wanted)
{
    Button<NoPin, false, 4> button;
    uint8_t seen = 0;
    for (char s : samples)
        if (s && button.sample(s == '#') == wanted)
            seen++;
    return seen == 1 ? wanted : Press::None;
}
static_assert(last_of("..##..", Press::Short) == Press::Short);
static_assert(last_of("..#...", Press::Short) == Press::None);   // a bounce
static_assert(last_of(".#####.", Press::Long) == Press::Long);   // once only
static_assert(last_of(".#####.", Press::Short) == Press::None);  // and not also short
} // namespace button_tests

#endif
