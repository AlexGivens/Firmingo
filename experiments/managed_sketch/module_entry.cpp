void setup();
void loop();

extern "C" __attribute__((used, section(".firmingo_entry.setup")))
void firmingo_sketch_setup() {
  setup();
}

extern "C" __attribute__((used, section(".firmingo_entry.loop")))
void firmingo_sketch_loop() {
  loop();
}
