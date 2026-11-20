// The C++ device library <pic/pic.hpp> on the simulator: pins, delays and
// the serial port. (The A/D converter and the EEPROM are checked on the
// part: tests/hardware.) Prints to the serial port and reports through
// LATD. For scripts/test-cxx.sh.
#pragma config FOSC = INTOSC, WDTE = OFF, MCLRE = ON, LVP = ON
#define _XTAL_FREQ 8000000UL
#include <pic/pic.hpp>

using Led0 = pic::Pin<'E', 0>;
using Led1 = pic::Pin<'E', 1>;
using Led3 = pic::Pin<'D', 1>;
using Exit = pic::Port<'D'>;

volatile uint8_t ticks;
extern "C" void __attribute__((interrupt)) isr()
{
    if (pic::Timer0::wrapped())
        ticks = ticks + 1;
}

int main()
{
    uint8_t failed = 0;
    pic::set_clock(pic::Clock::MHz8);
    Led0::digital();
    Led1::digital();
    Led0::output();
    Led1::output();
    Led3::output();
    Led0::set();
    Led1::clear();
    Led1::toggle();
    Led3::write(true);
    // (LATE3 has no latch; the device data says the bit reads 1.)
    if ((LATE & 7) != 0x03 || !Led0::driven() || !Led3::driven() || (TRISE & 3) != 0 || (TRISD & 2) != 0)
        failed = 1;
    Led0::write(false);
    if ((LATE & 7) != 0x02)
        failed = 2;

    pic::Uart::begin(9600);
    if (SP1BRG != 207)          // 8 MHz / (4 * 9600) - 1, rounded
        failed = 3;
    pic::Uart::write("pins ");
    pic::Uart::print(12345u);
    pic::Uart::write(' ');
    pic::Uart::print_hex(0xA5);
    pic::Uart::write('\n');
    pic::Uart::flush();

    pic::Timer0::begin(0, true);
    pic::enable_interrupts();
    while (ticks < 2) {
    }
    {
        pic::CriticalSection quiet;
        uint8_t before = ticks;
        pic::delay_ms(1);       // 2,000 cycles: the timer wraps several times
        if (ticks != before)
            failed = 4;
    }
    if (!INTCONbits.GIE)
        failed = 5;
    pic::disable_interrupts();

    Exit::tris() = 0;
    Exit::lat() = failed ? failed : 0x5A;
    SLEEP();
    return 0;
}
