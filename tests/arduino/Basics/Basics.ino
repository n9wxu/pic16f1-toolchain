// The Arduino core on the simulator: what it prints is compared with
// expected.txt by scripts/test-arduino.sh, and it ends with 0x5A in LATD
// if its own checks passed (the number of the first that failed, if not).

const byte pins[] = {PIN_RD4, PIN_RD5, PIN_RD6};
byte failed = 0;

void setup() {
  Serial.begin(9600);
  Serial.println(F("Arduino on a PIC16F1"));

  // Printing.
  Serial.print(12345);
  Serial.print(' ');
  Serial.print(-321);
  Serial.print(' ');
  Serial.print(255, HEX);
  Serial.print(' ');
  Serial.print(5, BIN);
  Serial.print(' ');
  Serial.print(1234567890UL);
  Serial.print(' ');
  Serial.print(-100000L);
  Serial.print(' ');
  Serial.println(3.14159, 3);

  // Pins: a constant pin (one instruction) and pins from a table.
  pinMode(PIN_RD7, OUTPUT);
  digitalWrite(PIN_RD7, HIGH);
  check(1, LATDbits.LATD7 == 1 && TRISDbits.TRISD7 == 0);
  digitalWrite(PIN_RD7, LOW);
  check(2, LATD == 0 || (LATD & 0x80) == 0);
  for (byte i = 0; i < sizeof pins; i++) {
    pinMode(pins[i], OUTPUT);
    digitalWrite(pins[i], i != 1);
  }
  check(3, (LATD & 0x70) == 0x50 && (TRISD & 0x70) == 0);
  pinMode(PIN_RB3, INPUT_PULLUP);
  check(4, (WPUB & 0x08) && (TRISB & 0x08) && !(ANSELB & 0x08));

  // Time: timer 0 counts the milliseconds a delay takes.
  unsigned long before = millis();
  delay(50);
  unsigned long took = millis() - before;
  check(5, took >= 46 && took <= 54);
  unsigned long us = micros();
  delayMicroseconds(2000);
  us = micros() - us;
  // (Counted in instructions, so the timer's interrupt lengthens it; and
  // what is measured includes a call of micros, which at 4 MHz is itself
  // half a millisecond of multiplying.)
  check(6, us >= 1900 && us <= 2700);
  Serial.print(F("50 ms took "));
  if (took >= 46 && took <= 54)
    Serial.println(F("that"));
  else
    Serial.println(took);

  // Arithmetic helpers.
  check(7, map(512, 0, 1023, 0, 255) == 127 && constrain(300, 0, 255) == 255);
  check(8, min(3, 7) == 3 && max(3, 7) == 7 && abs(-5) == 5 && sq(4) == 16);
  check(9, lowByte(0x1234) == 0x34 && highByte(0x1234) == 0x12 && word(0x12, 0x34) == 0x1234);
  byte flags = 0;
  bitSet(flags, 3);
  bitWrite(flags, 0, 1);
  check(10, flags == 9 && bitRead(flags, 3) == 1 && bit(4) == 16);
  randomSeed(42);
  long r = random(10, 20);
  check(11, r >= 10 && r < 20);
  check(12, twice(21) == 42 && isDigit('7') && !isAlpha('7'));

  Serial.println(failed ? F("FAILED") : F("passed"));
  Serial.flush();
  LATD = failed ? failed : 0x5A;
  __asm__("sleep");
}

void loop() {
}

void check(byte number, bool ok) {
  if (!ok && !failed)
    failed = number;
}

// (Defined after its use: the build declares a sketch's functions first.)
int twice(int n) {
  return 2 * n;
}
