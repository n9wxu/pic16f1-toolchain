/* A program written as for XC8: configuration pragmas before <xc.h>, user
 * IDs, EEPROM data and XC8's keywords. For scripts/test-xc8.sh. */
#pragma config FOSC = INTOSC, WDTE = OFF, PWRTE = ON, MCLRE = ON
#pragma config CP = OFF, CPD = OFF, BOREN = OFF, CLKOUTEN = OFF
#pragma config IESO = OFF, FCMEN = OFF
#pragma config WRT = OFF, PLLEN = OFF, STVREN = ON
#pragma config BORV = LO, LVP = ON
#pragma config IDLOC0 = 0x01, IDLOC1 = 0x02, IDLOC2 = 0x03, IDLOC3 = 0x04
#include <xc.h>
#include <stdint.h>
#define _XTAL_FREQ 500000UL
__EEPROM_DATA('p', 'k', '2', 'p', 'r', 'o', 'b', 'e');
__EEPROM_DATA(0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77);
static __bit flag;
__persistent unsigned char keep;
volatile unsigned char ticks;
void __interrupt() isr(void) { ticks++; }
void main(void)
{
    di();
    flag = 1;
    ei();
    __nop();
    LATD = 0x50 + flag + keep * 0;
    SLEEP();
}
