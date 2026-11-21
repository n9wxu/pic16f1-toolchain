// @NAME@: a starting point for the @DEVICE_UPPER@, as an Arduino sketch.
//
// Blinks the first LED of the F1 Evaluation Platform (LED_BUILTIN, pin
// RD1) and says so on the serial port (TX on RC6, 9600 baud). Pins are
// named PIN_RA0 to PIN_RE3; A0 to A13 are the analog inputs.

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(9600);
  Serial.println(F("@NAME@"));
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(500);
  digitalWrite(LED_BUILTIN, LOW);
  delay(500);
  Serial.println(millis());
}
