// Read the two bytes of a temperature sensor's register 0 over I2C.
#include <Wire.h>

void setup() {
  Wire.begin();
  Serial.begin(9600);
}

void loop() {
  Wire.beginTransmission(0x48);
  Wire.write(0);
  Wire.endTransmission(false);
  Wire.requestFrom(0x48, 2);
  int raw = Wire.read() << 8;
  raw |= Wire.read();
  Serial.println(raw >> 4);
  delay(1000);
}
