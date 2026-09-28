#include <Arduino.h>

// Destructive-to-the-running-sketch diagnostic. Byte 0xf4 acknowledges with
// 0xf5 and then never returns to the resident-owned core-1 loop boundary.
// Restoring another sketch after that trigger requires the RP2040 ROM path.
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
}

void loop() {
  digitalWrite(LED_BUILTIN, (millis() / 625) & 1 ? HIGH : LOW);
  for (unsigned i = 0; i < 64 && Serial.availableForWrite(); ++i) {
    const int incoming = Serial.read();
    if (incoming < 0) break;
    if (incoming == 0xf6) {
      Serial.write(uint8_t(0xf7));
      continue;
    }
    if (incoming == 0xf4) {
      Serial.write(uint8_t(0xf5));
      digitalWrite(LED_BUILTIN, HIGH);
      for (;;) __asm__ volatile("nop");
    }
    Serial.write(uint8_t(incoming));
  }
  delay(1);
}
