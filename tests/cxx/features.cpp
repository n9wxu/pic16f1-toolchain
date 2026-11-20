// The C++ language on the PIC16F1, without a standard library: each part
// checks itself, and main reports through LATD as the C tests do
// (0x5A if everything held, otherwise the number of the first failure).
// For scripts/test-cxx.sh.
#include <xc.h>
#include <stdint.h>
#include <stddef.h>

static uint8_t failed;
static void check(bool ok, uint8_t number)
{
    if (!ok && !failed)
        failed = number;
}

// Placement new, without <new>.
inline void *operator new(size_t, void *where) noexcept { return where; }

// ---- 1x: classes, construction and destruction in order --------------------
static uint8_t trace[12], traced;
struct Tracer {
    uint8_t id;
    explicit Tracer(uint8_t id) : id(id) { trace[traced++] = id; }
    ~Tracer() { trace[traced++] = id | 0x80; }
    Tracer(const Tracer &) = delete;
};
static void scopes()
{
    Tracer a(1);
    {
        Tracer b(2);
    }
    Tracer c(3);
}

// ---- 2x: virtual functions, pure and overridden; an abstract interface -----
struct Shape {
    virtual uint16_t area() const = 0;
    virtual uint8_t sides() const { return 0; }
    virtual ~Shape() {}
};
struct Square : Shape {
    uint8_t s;
    constexpr explicit Square(uint8_t s) : s(s) {}
    uint16_t area() const override { return uint16_t(s) * s; }
    uint8_t sides() const override { return 4; }
};
struct Circle final : Shape {
    uint8_t r;
    explicit Circle(uint8_t r) : r(r) {}
    uint16_t area() const override { return uint16_t(r) * r * 3; }
};
__attribute__((noinline)) static uint16_t total(const Shape *const *list, uint8_t n)
{
    uint16_t t = 0;
    while (n--)
        t += (*list++)->area();
    return t;
}

// ---- 3x: multiple and virtual inheritance ----------------------------------
struct Named { uint8_t name; virtual uint8_t who() const { return name; } };
struct Counted { uint16_t count; virtual uint16_t howMany() const { return count; } };
struct Both : Named, Counted {
    Both(uint8_t n, uint16_t c) { name = n; count = c; }
    uint16_t howMany() const override { return count + 1; }
};
struct Base { uint8_t tag = 7; };
struct Left : virtual Base { uint8_t l = 1; };
struct Right : virtual Base { uint8_t r = 2; };
struct Diamond : Left, Right { uint8_t d = 3; };

// ---- 4x: pointers to members ----------------------------------------------
struct Machine {
    uint8_t state = 0;
    void up() { state += 1; }
    void down() { state -= 1; }
    virtual void reset() { state = 100; }
    uint8_t twice(uint8_t x) const { return x * 2 + state; }
};
struct Sub : Machine { void reset() override { state = 200; } };

// ---- 5x: templates ---------------------------------------------------------
template <typename T, size_t N> struct Ring {
    T item[N];
    uint8_t head = 0, used = 0;
    bool push(T v)
    {
        if (used == N)
            return false;
        item[(head + used++) % N] = v;
        return true;
    }
    T pop()
    {
        T v = item[head];
        head = (head + 1) % N;
        used--;
        return v;
    }
};
template <typename T> constexpr T largest(T a) { return a; }
template <typename T, typename... Rest> constexpr T largest(T a, Rest... rest)
{
    T b = largest(rest...);
    return a > b ? a : b;
}
template <unsigned N> struct Factorial { static constexpr unsigned long value = N * Factorial<N - 1>::value; };
template <> struct Factorial<0> { static constexpr unsigned long value = 1; };
static_assert(Factorial<10>::value == 3628800UL, "computed by the compiler");

// ---- 6x: constexpr: a table made by the compiler, kept in flash ------------
struct Crc8Table {
    uint8_t v[16];
    constexpr Crc8Table() : v()
    {
        for (uint8_t i = 0; i < 16; i++) {
            uint8_t c = uint8_t(i << 4);
            for (uint8_t k = 0; k < 4; k++)
                c = uint8_t((c & 0x80) ? (c << 1) ^ 0x07 : c << 1);
            v[i] = c;
        }
    }
};
static constexpr Crc8Table crcTable;
__attribute__((noinline)) static uint8_t crc8(const uint8_t *p, uint8_t n)
{
    uint8_t c = 0;
    while (n--) {
        c ^= *p++;
        c = uint8_t(c << 4) ^ crcTable.v[c >> 4];
        c = uint8_t(c << 4) ^ crcTable.v[c >> 4];
    }
    return c;
}

// ---- 7x: lambdas and passing functions -------------------------------------
template <typename F> __attribute__((noinline)) static uint16_t fold(const uint8_t *p, uint8_t n, F f)
{
    uint16_t acc = 0;
    while (n--)
        acc = f(acc, *p++);
    return acc;
}

// ---- 8x: operators, references, overloads, enum class, namespaces ----------
namespace fixed {
struct Q8 {
    int16_t raw;
    constexpr Q8(int16_t whole, uint8_t frac = 0) : raw(int16_t(whole * 256 + frac)) {}
    constexpr Q8 operator+(Q8 o) const { return fromRaw(raw + o.raw); }
    constexpr Q8 operator*(Q8 o) const { return fromRaw(int16_t((int32_t(raw) * o.raw) >> 8)); }
    constexpr bool operator==(Q8 o) const { return raw == o.raw; }
    Q8 &operator+=(Q8 o) { raw += o.raw; return *this; }
    static constexpr Q8 fromRaw(int16_t r) { Q8 q(0); q.raw = r; return q; }
};
}
enum class Mode : uint8_t { Off, Slow, Fast };
static uint8_t rate(Mode m)
{
    switch (m) {
    case Mode::Off: return 0;
    case Mode::Slow: return 10;
    case Mode::Fast: return 200;
    }
    return 255;
}
static void swap(uint8_t &a, uint8_t &b) { uint8_t t = a; a = b; b = t; }
static uint8_t width(uint8_t) { return 1; }
static uint8_t width(uint16_t) { return 2; }
static uint8_t width(uint32_t) { return 4; }
static uint8_t width(const char *) { return 9; }

// ---- 9x: objects with constructors at file scope, and local statics --------
static uint8_t order[4], ordered;
struct Global {
    uint8_t v;
    explicit Global(uint8_t n) : v(n) { order[ordered++] = n; }
};
static Global first(11);
static Global second(22);
__attribute__((init_priority(200))) static Global early(5);
static uint8_t &counter()
{
    static uint8_t made = 0;
    static Global once((made++, 33));
    return made;
}

// ---- 10x: an interrupt handler written in C++ ------------------------------
struct Ticker {
    static volatile uint8_t ticks;
    static void tick() { ticks = ticks + 1; }
};
volatile uint8_t Ticker::ticks;
extern "C" void __attribute__((interrupt)) isr()
{
    if (INTCONbits.TMR0IF) {
        INTCONbits.TMR0IF = 0;
        Ticker::tick();
    }
}

int main()
{
    // 1x
    scopes();
    const uint8_t want[] = {1, 2, 0x82, 3, 0x83, 0x81};
    check(traced == 6, 10);
    for (uint8_t i = 0; i < 6; i++)
        check(trace[i] == want[i], 11);

    // 2x
    Square sq(7);
    Circle ci(2);
    const Shape *shapes[] = {&sq, &ci, &sq};
    check(total(shapes, 3) == 49 + 12 + 49, 20);
    check(shapes[0]->sides() == 4 && shapes[1]->sides() == 0, 21);
    alignas(Square) uint8_t room[sizeof(Square)];
    Shape *made = new (room) Square(9);
    check(made->area() == 81, 22);
    made->~Shape();

    // 3x
    Both both(65, 1000);
    Named *n = &both;
    Counted *c = &both;
    check(n->who() == 65 && c->howMany() == 1001, 30);
    check(static_cast<Both *>(c) == &both, 31);
    Diamond dia;
    Left *l = &dia;
    Right *r = &dia;
    l->tag = 42;
    check(r->tag == 42 && dia.l + dia.r + dia.d == 6, 32);
    check(sizeof(void *) == 2 && sizeof(int) == 2 && sizeof(long) == 4, 33);

    // 4x
    Machine m;
    Sub s;
    void (Machine::*step)() = &Machine::up;
    (m.*step)();
    (m.*step)();
    step = &Machine::down;
    (m.*step)();
    check(m.state == 1, 40);
    void (Machine::*virt)() = &Machine::reset;
    Machine *pm = &s;
    (pm->*virt)();
    (m.*virt)();
    check(s.state == 200 && m.state == 100, 41);
    uint8_t (Machine::*calc)(uint8_t) const = &Machine::twice;
    check((m.*calc)(5) == 110, 42);
    uint8_t Machine::*field = &Machine::state;
    m.*field = 9;
    check(m.state == 9, 43);
    void (Machine::*table[])() = {&Machine::up, &Machine::up, &Machine::down, &Machine::reset};
    for (auto f : table)
        (m.*f)();
    check(m.state == 100, 44);
    check(virt != step && virt == &Machine::reset && step != nullptr, 45);

    // 5x
    Ring<uint16_t, 4> ring;
    for (uint16_t v = 1000; ring.push(v); v += 1000)
        ;
    check(ring.used == 4 && ring.pop() == 1000 && ring.pop() == 2000, 50);
    ring.push(5000);
    check(ring.pop() == 3000 && ring.pop() == 4000 && ring.pop() == 5000 && ring.used == 0, 51);
    check(largest(3, 9, 4) == 9 && largest(7UL, 70000UL) == 70000UL, 52);
    check(Factorial<5>::value == 120, 53);

    // 6x
    const uint8_t text[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    check(crc8(text, 9) == 0xF4, 60); // the check value of CRC-8
    check(uint16_t(&crcTable) >= 0x8000, 61); // in flash, not copied to RAM

    // 7x
    uint8_t bias = 3;
    check(fold(text, 9, [](uint16_t a, uint8_t x) { return uint16_t(a + (x - '0')); }) == 45, 70);
    check(fold(text, 9, [bias](uint16_t a, uint8_t x) { return uint16_t(a + bias); }) == 27, 71);
    uint8_t seen = 0;
    fold(text, 9, [&seen](uint16_t a, uint8_t x) { seen++; return a; });
    check(seen == 9, 72);
    auto twice = [](auto x) { return x + x; };
    check(twice(uint8_t(100)) == 200 && twice(40000UL) == 80000UL, 73);

    // 8x
    using fixed::Q8;
    constexpr Q8 half(0, 128);
    Q8 q = Q8(3) * half + Q8(1, 64);
    check(q == Q8(2, 192), 80);
    q += half;
    check(q.raw == 3 * 256 + 64, 81);
    check(rate(Mode::Slow) == 10 && rate(Mode::Fast) == 200, 82);
    uint8_t x = 1, y = 2;
    swap(x, y);
    check(x == 2 && y == 1, 83);
    check(width(uint8_t(0)) + width(uint16_t(0)) + width(uint32_t(0)) + width("") == 16, 84);
    uint8_t pair[2] = {4, 6};
    auto &[p0, p1] = pair;
    p0 += p1;
    check(pair[0] == 10, 85);

    // 9x: by priority, then in the order written; the local static on first use
    check(ordered == 3 && order[0] == 5 && order[1] == 11 && order[2] == 22, 90);
    check(counter() == 1 && counter() == 1 && ordered == 4 && order[3] == 33, 91);
    check(first.v + second.v + early.v == 38, 92);

    // 10x
    OPTION_REG = 0x08; // timer 0 from the instruction clock, no prescaler
    INTCONbits.TMR0IE = 1;
    INTCONbits.GIE = 1;
    while (Ticker::ticks < 3)
        ;
    INTCONbits.GIE = 0;
    check(Ticker::ticks >= 3, 100);

    LATD = failed ? failed : 0x5A;
    SLEEP();
    return 0;
}
