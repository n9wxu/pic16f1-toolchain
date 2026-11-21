// Send a counter out of the SPI port.
#include <SPI.h>

byte count;

void setup() {
  pinMode(SS, OUTPUT);
  SPI.begin();
}

void loop() {
  digitalWrite(SS, LOW);
  SPI.transfer(count++);
  digitalWrite(SS, HIGH);
  delay(10);
}
