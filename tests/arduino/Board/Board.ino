// The Arduino core on the F1 Evaluation Platform itself: what the
// simulator cannot check. It reads the potentiometer and the supply,
// talks to the MCP9800 with Wire, writes the EEPROM, puts PWM on an LED
// and counts a pin interrupt, leaves what it found in the variables below
// (a debugger reads them), prints it on the serial pin, and calls
// hw_done with 0x5A in LATD if all of it was as expected.
#include <Wire.h>
#include <EEPROM.h>
#include <SPI.h>

int pot;                 // 0 to 1023
int reference;           // the 1.024 V reference against VDD: about 327 at 3.2 V
int sixteenths;          // the temperature, in sixteenths of a degree Celsius
byte sensor;             // the address the MCP9800 answered at
volatile byte edges;     // interrupts counted on RB0
byte failed;

void countEdge() {
  edges++;
}

__attribute__((noinline)) void hw_done() {
  for (;;) {
  }
}

void setup() {
  Serial.begin(9600);

  pot = analogRead(POT);
  reference = analogRead(A_FVR);
  check(1, reference > 250 && reference < 420);

  // The sensor: found by asking each address it can have, set to 12
  // bits, and read.
  Wire.begin();
  for (byte address = 0x48; address <= 0x4F && !sensor; address++) {
    Wire.beginTransmission(address);
    Wire.write(1);
    Wire.write(0x60);
    if (Wire.endTransmission() == 0)
      sensor = address;
  }
  check(2, sensor != 0);
  delay(300);
  Wire.beginTransmission(sensor);
  Wire.write(0);
  check(3, Wire.endTransmission(false) == 0);
  check(4, Wire.requestFrom(sensor, (byte)2) == 2);
  sixteenths = (int)(Wire.read() << 8 | Wire.read()) >> 4;
  check(5, sixteenths > 5 * 16 && sixteenths < 45 * 16);
  // Nothing answers here.
  Wire.beginTransmission(0x23);
  check(6, Wire.endTransmission() == 2);

  // The EEPROM: bytes, and a number of four.
  byte was = EEPROM.read(10);
  EEPROM.write(10, was + 1);
  check(7, EEPROM.read(10) == (byte)(was + 1));
  EEPROM[11] = 0xA5;
  check(8, EEPROM[11] == 0xA5);
  unsigned long stored = 0x12345678UL + was, back = 0;
  EEPROM.put(12, stored);
  EEPROM.get(12, back);
  check(9, back == stored);

  // PWM on LED D1: a quarter on.
  analogWrite(LED_D1, 64);
  check(10, CCPR4L == 64 && (CCP4CON & 0x0F) == 0x0C && T2CONbits.TMR2ON);

  // A pin interrupt: RB0, driven by this program itself.
  // (Driven before the interrupt is attached: a floating pin makes edges
  // of its own.)
  digitalWrite(PIN_RB0, LOW);
  pinMode(PIN_RB0, OUTPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_RB0), countEdge, RISING);
  // (Apart, so that the handler has taken one edge before the next.)
  for (byte i = 0; i < 2; i++) {
    digitalWrite(PIN_RB0, LOW);
    delay(1);
    digitalWrite(PIN_RB0, HIGH);
    delay(1);
  }
  check(11, edges == 2);
  detachInterrupt(digitalPinToInterrupt(PIN_RB0));

  // Time against the part's own timer 1 is checked by the debugger's
  // clock: half a second here.
  unsigned long before = millis();
  delay(500);
  check(12, millis() - before >= 499 && millis() - before <= 501);

  Serial.print(F("pot "));
  Serial.print(pot);
  Serial.print(F("  reference "));
  Serial.print(reference);
  Serial.print(F("  sensor 0x"));
  Serial.print(sensor, HEX);
  Serial.print(F("  temperature "));
  Serial.print(sixteenths / 16.0, 1);
  Serial.println(failed ? F("  FAILED") : F("  passed"));
  Serial.flush();

  pinMode(PIN_RD0, OUTPUT);
  LATD = failed ? failed : 0x5A;
  TRISD = 0;
  hw_done();
}

void loop() {
}

void check(byte number, bool ok) {
  if (!ok && !failed)
    failed = number;
}
