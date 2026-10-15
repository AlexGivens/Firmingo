#include <Arduino.h>

// Volatile keeps real RAM loads/stores visible in this independently linked
// proof. No constructors, heap, SDK startup or library code are required.
volatile uint32_t initialized_counter = 7;
volatile uint32_t loop_counter;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, initialized_counter == 7 && loop_counter == 0);
}

void loop() {
  ++initialized_counter;
  ++loop_counter;
  digitalWrite(LED_BUILTIN, loop_counter & 1);
  delay(100);
}
