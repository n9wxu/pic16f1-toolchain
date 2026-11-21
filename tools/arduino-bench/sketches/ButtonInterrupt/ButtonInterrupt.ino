// The same sketch for both boards; only the pins have other names.
#ifdef ARDUINO_ARCH_PIC16F1
const byte buttonPin = BUTTON_S1, pwmPin = LED_D2, interruptPin = PIN_RB0, analogPin = POT;
#else
const byte buttonPin = 2, pwmPin = 9, interruptPin = 2, analogPin = A0;
#endif

volatile byte state = LOW;

void blink() {
  state = !state;
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(interruptPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(interruptPin), blink, CHANGE);
}

void loop() {
  digitalWrite(LED_BUILTIN, state);
}
