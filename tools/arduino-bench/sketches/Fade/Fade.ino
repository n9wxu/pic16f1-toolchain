// The same sketch for both boards; only the pins have other names.
#ifdef ARDUINO_ARCH_PIC16F1
const byte buttonPin = BUTTON_S1, pwmPin = LED_D2, interruptPin = PIN_RB0, analogPin = POT;
#else
const byte buttonPin = 2, pwmPin = 9, interruptPin = 2, analogPin = A0;
#endif

int brightness = 0;
int fadeAmount = 5;

void setup() {
  pinMode(pwmPin, OUTPUT);
}

void loop() {
  analogWrite(pwmPin, brightness);
  brightness = brightness + fadeAmount;
  if (brightness <= 0 || brightness >= 255) {
    fadeAmount = -fadeAmount;
  }
  delay(30);
}
