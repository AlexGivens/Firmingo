#include <Arduino.h>

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  Serial.println("Sketch A ready");
}

void loop() {
  digitalWrite(LED_BUILTIN, (millis() / 250) & 1 ? HIGH : LOW);
  for (unsigned i = 0; i < 64 && Serial.availableForWrite(); ++i) {
    const int incoming = Serial.read();
    if (incoming < 0) break;
    Serial.write(uint8_t(incoming));
  }
  delay(1);
}
