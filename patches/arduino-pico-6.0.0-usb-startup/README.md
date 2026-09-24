# Arduino-Pico 6.0.0 controlled USB startup patch

Opt-in build define: `FIRMINGO_USB_DEFERRED_START`. This requires Pico SDK USB,
bare metal, `DISABLE_USB_SERIAL`, and no Picotool USB reset interface. Other
profiles are rejected during compilation. Only Nano RP2040 Connect is currently
build-supported by this repository.

- `USB.prepare()` initializes its mutex idempotently without initializing TinyUSB
  or enabling the USB pull-up. The patched main calls it before sketch startup.
- `USB.begin()` uses prepare then initializes the controller normally. The sketch
  calls it after DHCP, NCM netif and all listeners are ready.
- NCM begin requires an uninitialized controller, registers its descriptors and
  skips its normal disconnect/connect pair under the opt-in define.
- The bare-metal NCM receive worker retains its ten-packet bound. When that budget
  is exhausted it releases the USB mutex and calls the SDK async-context wake API
  to schedule another run, so queued packet 11 is renewed.
- Saturating boot-lifetime counters report worker runs, received frames, deferred
  callbacks, maximum received frames in one worker run, budget exhaustion and wake
  requests through the patched NCM class. V6 also counts worker attempts that
  could not acquire the USB mutex; it does not yet change that retry behavior.
- Default core/NCM behavior is retained when the define is absent. NCM `end()`
  remains upstream teardown behavior; the qualification app never calls it and
  does not support descriptor changes or restarting Ethernet at runtime.

This is a project patch, not an existing upstream deferred-connect API. It does
not certify iOS authorization or attachment. USB.prepare() is a startup-only API,
not a synchronization operation to call while USB processing is running.

`manifest.json` pins unmodified and patched SHA-256 for every touched file and
the patch itself. Upstream source snapshots and LGPL license are identifiable in
`third_party/arduino-pico-usb-startup/`. `tools/usb_core.py` verifies the installed
6.0.0 files and applies the patch without fuzz to real copies in a separate
Arduino data/build overlay. Other SDK files/tools are dependency links; installed
core sources are never patched. The compile report records the overlay and pins.

Native tests compile the actual patched main USB block, USB prepare/begin,
NCM begin, NCM receive worker and reference setup bodies against fake low-level
effects, for deferred and default profiles. They check ordering, idempotence,
failure behavior, receive-worker continuation, wake requests and mutex ordering;
they do not simulate USB packet
timing. Full firmware builds check headers and SDK translation units.
Hardware evidence belongs in `docs/evidence/usb-startup.md`.
