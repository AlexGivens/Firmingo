#include <Arduino.h>

// Experimental, bounded stall probe. A 0xf0 byte elicits 0xf1 and then
// holds this sketch core in delay() for five seconds before returning to
// the resident-owned loop boundary. A 0xf2 byte elicits 0xf3 as a quick
// identity check; all other bytes are echoed unchanged.
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
}

void loop() {
  digitalWrite(LED_BUILTIN, (millis() / 375) & 1 ? HIGH : LOW);
  for (unsigned i = 0; i < 64 && Serial.availableForWrite(); ++i) {
    const int incoming = Serial.read();
    if (incoming < 0) break;
    if (incoming == 0xf0) {
      Serial.write(uint8_t(0xf1));
      delay(5000);
      break;
    }
    if (incoming == 0xf2) {
      Serial.write(uint8_t(0xf3));
      continue;
    }
    Serial.write(uint8_t(incoming));
  }
  delay(1);
}
