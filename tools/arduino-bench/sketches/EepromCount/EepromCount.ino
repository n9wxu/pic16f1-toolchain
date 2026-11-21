// Count resets in the EEPROM.
#include <EEPROM.h>

void setup() {
  Serial.begin(9600);
  byte count = EEPROM.read(0) + 1;
  EEPROM.update(0, count);
  Serial.println(count);
}

void loop() {
}
