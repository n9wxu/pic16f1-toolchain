// The parts of the C++ standard library that are headers only (libc++,
// installed by scripts/build-cxx-headers.sh), used without the C library
// being linked. Reports through LATD: 0x5A, or the number of the first
// failure. For scripts/test-cxx.sh.
#include <xc.h>
#include <algorithm>
#include <array>
#include <bit>
#include <bitset>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

static uint8_t failed;
static void check(bool ok, uint8_t number)
{
    if (!ok && !failed)
        failed = number;
}

volatile uint8_t seed = 7;

static std::optional<uint8_t> digit(char c)
{
    if (c >= '0' && c <= '9')
        return uint8_t(c - '0');
    return std::nullopt;
}

static std::tuple<uint8_t, uint8_t> divmod(uint8_t a, uint8_t b) { return {uint8_t(a / b), uint8_t(a % b)}; }

__attribute__((noinline)) static uint16_t sum(std::span<const uint8_t> s)
{
    return std::accumulate(s.begin(), s.end(), uint16_t(0));
}

struct Reading { uint16_t raw; };
struct Fault { uint8_t code; };
using Result = std::variant<Reading, Fault>;
static Result sample(uint8_t channel)
{
    if (channel > 3)
        return Fault{uint8_t(channel + 100)};
    return Reading{uint16_t(channel * 1000)};
}

int main()
{
    // <array>, <algorithm>
    std::array<uint8_t, 8> a{};
    uint8_t x = seed;
    for (auto &v : a) {
        x = uint8_t(x * 37 + 11);
        v = x;
    }
    std::array<uint8_t, 8> b = a;
    // An insertion sort from the library's pieces. (std::sort itself works,
    // but libc++'s is written for large machines: about 8,000 words.)
    for (auto i = b.begin(); i != b.end(); ++i)
        std::rotate(std::upper_bound(b.begin(), i, *i), i, i + 1);
    check(std::is_sorted(b.begin(), b.end()), 1);
    check(sum(a) == sum(b), 2);
    check(*std::max_element(a.begin(), a.end()) == b.back(), 3);
    check(std::find(a.begin(), a.end(), b[3]) != a.end(), 4);
    check(std::count_if(b.begin(), b.end(), [](uint8_t v) { return v >= 128; }) ==
              b.end() - std::lower_bound(b.begin(), b.end(), 128), 5);
    std::reverse(b.begin(), b.end());
    check(b.front() >= b.back(), 6);
    check(std::clamp<int>(seed * 100, 0, 255) == 255 && std::min(seed + 0, 9) == 7, 7);

    // <optional>, <tuple>, structured bindings
    check(digit('7').value_or(99) == 7 && !digit('x') && digit('x').value_or(99) == 99, 10);
    auto [q, r] = divmod(47, seed);
    check(q == 6 && r == 5, 11);

    // <span>, <string_view>
    const uint8_t raw[] = {1, 2, 3, 4, 5, 6};
    std::span<const uint8_t> s(raw);
    check(sum(s) == 21 && sum(s.subspan(2, 3)) == 12 && s.size() == 6, 20);
    constexpr std::string_view cmd = "SET LED=1";
    check(cmd.starts_with("SET") && cmd.find('=') == 7 && cmd.substr(4, 3) == "LED", 21);

    // <bit>, <bitset>, <limits>, <type_traits>
    check(std::popcount(uint8_t(seed * 37)) == 2 && std::countl_zero(uint16_t(seed)) == 13, 30);
    check(std::has_single_bit(uint8_t(seed + 1)) && std::rotl(uint8_t(0x81), 1) == 0x03, 31);
    std::bitset<12> bits;
    bits.set(seed).set(11);
    check(bits.count() == 2 && bits.test(7) && bits.to_ulong() == 0x880, 32);
    check(std::numeric_limits<int>::max() == 32767 && std::numeric_limits<long>::digits == 31, 33);
    static_assert(std::is_same_v<std::make_unsigned_t<int>, unsigned>);
    static_assert(sizeof(std::size_t) == 2 && sizeof(std::ptrdiff_t) == 2);

    // <variant>, <utility>
    Result good = sample(2), bad = sample(seed);
    check(std::holds_alternative<Reading>(good) && std::get<Reading>(good).raw == 2000, 40);
    check(std::get_if<Fault>(&bad) && std::get_if<Fault>(&bad)->code == 107, 41);
    auto p = std::make_pair(uint8_t(1), uint16_t(2));
    std::swap(p.first, x);
    check(p.second == 2 && std::exchange(x, 0) == 1, 42);

    LATD = failed ? failed : 0x5A;
    SLEEP();
    return 0;
}
