/* Host stand-in for <xc.h>, used only to compute expected program output
 * with the native compiler. It models just enough of the port B latch for
 * the test programs; nothing here describes a real device. */
#ifndef HOST_XC_H
#define HOST_XC_H
#include <stdint.h>

typedef union {
    uint8_t byte;
    struct { unsigned LATB0:1, LATB1:1, LATB2:1, LATB3:1, LATB4:1, LATB5:1, LATB6:1, LATB7:1; };
    struct { unsigned RB0:1, RB1:1, RB2:1, RB3:1, RB4:1, RB5:1, RB6:1, RB7:1; };
} host_port_t;

extern host_port_t host_portb;
extern uint8_t host_trisb;
#define LATB (host_portb.byte)
#define LATBbits host_portb
#define PORTBbits host_portb
#define TRISB host_trisb

#endif
