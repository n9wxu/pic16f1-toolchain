// The same work on both boards, each piece timed with micros() and
// printed with what it computed (which must be the same on both).
// tools/arduino_bench.py builds and runs it.

volatile byte sink;
volatile byte variablePin = LED_BUILTIN;
byte buffer[256];

void report(const char *name, unsigned long result, unsigned long us) {
  Serial.print(name);
  Serial.print(' ');
  Serial.print(result);
  Serial.print(' ');
  Serial.println(us);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  unsigned long t;

  // 1,000 pairs of digitalWrite on a pin that is a constant.
  t = micros();
  for (unsigned int i = 0; i < 1000; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    digitalWrite(LED_BUILTIN, LOW);
  }
  report("write-constant-pin", 0, micros() - t);

  // The same on a pin in a variable.
  t = micros();
  for (unsigned int i = 0; i < 1000; i++) {
    digitalWrite(variablePin, HIGH);
    digitalWrite(variablePin, LOW);
  }
  report("write-variable-pin", 0, micros() - t);

  // 1,000 digitalRead.
  unsigned int high = 0;
  t = micros();
  for (unsigned int i = 0; i < 1000; i++)
    high += digitalRead(LED_BUILTIN);
  t = micros() - t;
  report("read-constant-pin", high, t);

  // 1,000 calls of millis.
  unsigned long last = 0;
  t = micros();
  for (unsigned int i = 0; i < 1000; i++)
    last += millis() & 1;
  t = micros() - t;
  report("millis", 0, t);

  // Bytes: a CRC-8 of 1,024 bytes, bit by bit.
  byte crc = 0;
  t = micros();
  for (unsigned int i = 0; i < 1024; i++) {
    crc ^= (byte)(i * 7);
    for (byte b = 0; b < 8; b++)
      crc = crc & 0x80 ? (byte)(crc << 1) ^ 0x07 : (byte)(crc << 1);
  }
  t = micros() - t;
  report("crc8", crc, t);

  // 16 bits: sums of squares, and 200 divisions.
  unsigned int sum = 0;
  t = micros();
  for (unsigned int i = 0; i < 1000; i++)
    sum += i * i;
  for (unsigned int i = 1; i <= 200; i++)
    sum += 50000u / i;
  t = micros() - t;
  report("int16", sum, t);

  // 32 bits: 1,000 steps of a multiply-and-add generator, 200 divisions.
  unsigned long x = 1;
  t = micros();
  for (unsigned int i = 0; i < 1000; i++)
    x = x * 1103515245UL + 12345UL;
  for (unsigned int i = 1; i <= 200; i++)
    x += 4000000000UL / i;
  t = micros() - t;
  report("int32", x, t);

  // Floating point: 500 terms of a series for pi.
  float pi = 0, sign = 4;
  t = micros();
  for (unsigned int i = 0; i < 500; i++) {
    pi += sign / (2 * i + 1);
    sign = -sign;
  }
  t = micros() - t;
  report("float", (unsigned long)(pi * 100000), t);

  // Memory: the primes below 256 by a sieve, ten times.
  byte primes = 0;
  t = micros();
  for (byte round = 0; round < 10; round++) {
    primes = 0;
    memset(buffer, 1, sizeof buffer);
    for (unsigned int i = 2; i < 256; i++) {
      if (!buffer[i])
        continue;
      primes++;
      for (unsigned int j = i * 2; j < 256; j += i)
        buffer[j] = 0;
    }
  }
  t = micros() - t;
  report("sieve", primes, t);

  // Sorting: 64 numbers of 16 bits, by insertion.
  unsigned int *numbers = (unsigned int *)buffer;
  unsigned int seed = 12345;
  for (byte i = 0; i < 64; i++) {
    seed = seed * 25173u + 13849u;
    numbers[i] = seed;
  }
  t = micros();
  for (byte i = 1; i < 64; i++) {
    unsigned int value = numbers[i];
    byte j = i;
    while (j > 0 && numbers[j - 1] > value) {
      numbers[j] = numbers[j - 1];
      j--;
    }
    numbers[j] = value;
  }
  t = micros() - t;
  report("sort", numbers[0] + numbers[63], t);

  Serial.println("done");
  Serial.flush();
#ifdef ARDUINO_ARCH_PIC16F1
  // (The simulator stops at SLEEP.)
  LATD = 0x5A;
  __asm__("sleep");
#else
  Serial.write(4);
  Serial.flush();
#endif
}

void loop() {
}
