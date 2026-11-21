// The same sketch for both boards; only the pins have other names.
#ifdef ARDUINO_ARCH_PIC16F1
const byte buttonPin = BUTTON_S1, pwmPin = LED_D2, interruptPin = PIN_RB0, analogPin = POT;
#else
const byte buttonPin = 2, pwmPin = 9, interruptPin = 2, analogPin = A0;
#endif

void setup() {
  Serial.begin(9600);
  pinMode(buttonPin, INPUT);
}

void loop() {
  int buttonState = digitalRead(buttonPin);
  Serial.println(buttonState);
  delay(1);
}
