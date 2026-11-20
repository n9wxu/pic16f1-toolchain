// C++ on the part itself: the language's run-time pieces (constructors of
// globals, virtual calls) and the device library <pic/pic.hpp> where the
// simulator cannot check it: the length of a delay against a timer, the
// data EEPROM, and the A/D converter against the fixed voltage reference.
// For a PIC16(L)F1937 on the F1 Evaluation Platform; run by tools/hwtest.py,
// which reads the code this leaves in LATD.
#pragma config FOSC = INTOSC, WDTE = OFF, PWRTE = ON, MCLRE = ON
#pragma config BOREN = OFF, CLKOUTEN = OFF, IESO = OFF, FCMEN = OFF
#pragma config WRT = OFF, PLLEN = OFF, STVREN = ON, BORV = LO, LVP = ON
#define _XTAL_FREQ 500000UL
#include <pic/pic.hpp>

using Led0 = pic::Pin<'E', 0>;
using Led1 = pic::Pin<'E', 1>;
using Led2 = pic::Pin<'E', 2>;

struct Source {
    virtual uint8_t next() = 0;
};
struct Counter : Source {
    uint8_t n;
    explicit Counter(uint8_t start) : n(start) {}
    uint8_t next() override { return n++; }
};
static Counter counter(0x30);       // constructed before main
volatile bool use_counter = true;

volatile uint8_t timer_ticks;       // what the checks measured, for the report
volatile uint16_t reference;
volatile uint8_t stored[4];

extern "C" __attribute__((noinline)) void hw_done()
{
    for (;;) {
        Led2::toggle();
        pic::delay_ms(250);
    }
}

int main()
{
    uint8_t failed = 0;
    TRISD = 0;
    LATD = 0xFF;
    Led0::digital();
    Led1::digital();
    Led2::digital();
    Led0::output();
    Led1::output();
    Led2::output();
    Led0::set();
    Led1::clear();
    if (!Led0::driven() || Led1::driven())
        failed = 1;

    // A virtual call on an object a constructor set up.
    Source *source = use_counter ? &counter : nullptr;
    if (source->next() != 0x30 || source->next() != 0x31)
        failed = 2;

    // 100 ms against timer 0: 125,000 instruction cycles a second, divided
    // by 256, is a count every 2.048 ms, so about 49 counts.
    pic::Timer0::begin(7);
    pic::delay_ms(100);
    timer_ticks = pic::Timer0::count();
    if (timer_ticks < 47 || timer_ticks > 52)
        failed = 3;

    // The EEPROM keeps what is written.
    for (uint8_t i = 0; i < 4; i++)
        pic::Eeprom::update(uint8_t(0x40 + i), source->next());
    for (uint8_t i = 0; i < 4; i++) {
        stored[i] = pic::Eeprom::read(uint8_t(0x40 + i));
        if (stored[i] != 0x32 + i)
            failed = 4;
    }

    // The 1.024 V reference measured against the supply: between 1.8 V and
    // 5.5 V that is a reading of 190 to 582.
    pic::Adc::begin();
    pic::Adc::fixed_reference_on(1);
    reference = pic::Adc::read(pic::Adc::fixed_reference);
    if (reference < 190 || reference > 582)
        failed = 5;

    LATD = failed ? failed : 0x5A;
    hw_done();
}
