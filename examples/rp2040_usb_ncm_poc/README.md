# Arduino Nano RP2040 Connect USB-NCM proof of concept

This sketch is specifically configured for the **Arduino Nano RP2040 Connect**.
It makes the board appear as a USB Network Control Model (NCM) Ethernet
adapter. The Nano uses `192.168.7.1`, provides DHCP to the USB host, serves a
Safari-compatible byte-stream test, and exposes raw TCP source and echo
services. Its onboard NINA-W102 Wi-Fi module is not used.

## Baseline status

This sketch is preserved byte-for-byte as the imported predecessor. Build pins
and its checksum are recorded in `../../boards/nano_rp2040_connect/baseline.json`.
Use [the development guide](../../docs/development.md) for pinned CLI builds and
[the testing guide](../../docs/testing.md) for the new exact-byte smoke harness.
The portable stream component is not integrated into this sketch yet.

Earlier comments/instructions in the sketch are historical hypotheses, not proof:
the latest user report still needed RESET after iOS connection; the LED does not
independently prove NCM binding or a DHCP lease. New clients replace old clients,
and short socket writes can lose bytes. DHCP still advertises router and DNS.
No Internet coexistence or cold-attachment fix is established.

## Requirements

- An Arduino Nano RP2040 Connect
- A USB-C-to-Micro-USB **data** cable for direct connection to a USB-C iPhone or iPad—the Nano RP2040 Connect has a Micro-USB connector, not USB-C
- A USB-C iPhone or iPad
- Arduino IDE 2.x
- Earle F. Philhower's **Raspberry Pi Pico/RP2040/RP2350** Arduino core **6.0.0**, pinned for this repository

Boards Manager URL:

```text
https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
```

## Build and flash

1. Add the Boards Manager URL under Arduino IDE **Settings → Additional Boards Manager URLs**.
2. Install **Raspberry Pi Pico/RP2040/RP2350** by Earle F. Philhower.
3. Open `RP2040_USB_NCM_POC/RP2040_USB_NCM_POC.ino`.
4. Select **Tools → Board → Raspberry Pi RP2040 Boards → Arduino Nano RP2040 Connect**. Do not choose the similarly named board from Arduino's Mbed OS Nano Boards package; this sketch needs the Philhower core's NCM support.
5. Select **Tools → USB Stack → Pico SDK**. Do not select Adafruit TinyUSB.
6. Leave the normal CPU setting at 125 MHz. Any flash layout is sufficient; **16MB (no FS)** is appropriate because this demonstration does not use a flash filesystem.
7. Select the Nano's serial port and upload.
8. If upload cannot find the board, double-tap its **RESET** button. An `RPI-RP2` drive should appear; select the newly detected port/target and upload again.

No third-party Arduino library is required; NCM, lwIP, WiFi-compatible sockets,
and the DHCP server come with the Arduino-Pico core.

## iPhone/iPad test

1. Disconnect the Nano from the development computer.
2. Connect its Micro-USB port directly to an unlocked USB-C iPhone or iPad using a USB-C-to-Micro-USB data cable. A USB-C adapter plus a known-good Micro-USB data cable is also acceptable.
3. Wait for automatic addressing. The LED is only a USB status hint, not proof of NCM or DHCP readiness.
4. In Safari, open `http://192.168.7.1/` explicitly. Do not use HTTPS.
5. The page should report `receiving`, and its byte count, rate, and checksum should continuously change.

Loading the page over the selected USB interface demonstrates usable IP/TCP/HTTP;
check the assigned address separately to establish automatic DHCP. An increasing
body byte count shows traffic, not exact integrity. The Nano's NINA-W102 does
not use Wi-Fi during this test.

The Arduino-Pico DHCP implementation normally assigns the phone or tablet an address from
`192.168.7.16` through `192.168.7.23`.

The sketch initializes lwIP and opens its DHCP listener before exposing the NCM
interface in the sketch. Once NCM initialization creates its network interface,
the existing listener is restricted to that interface. This ordering improved
reported behavior but has not resolved automatic startup. Pre-sketch USB startup
also needs tracing before any race or single-attachment guarantee can be claimed.

## Raw services for an IDE

### TCP 5000: deterministic source

The Nano emits one 272-byte record every 50 ms. TCP is a stream, so a receiver
must buffer until an entire record is available; one `receive` call is not
guaranteed to return one record.

| Offset | Size | Meaning |
|---:|---:|---|
| 0 | 4 | ASCII `PICO` |
| 4 | 4 | Little-endian sequence number |
| 8 | 4 | Little-endian Nano uptime in milliseconds |
| 12 | 2 | Little-endian payload length: 256 |
| 14 | 2 | Reserved flags: zero |
| 16 | 256 | Byte `i XOR lowByte(sequence)` for payload position `i` |

`verify_stream.py` validates 100 records from macOS, Linux, Windows, or a Python-capable client:

```bash
python3 verify_stream.py 192.168.7.1
```

### TCP 5001: exact echo

Port 5001 attempts to echo received bytes unchanged. The baseline ignores short
write return values, so exact integrity under backpressure is unverified. The
new smoke harness checks exact bytes rather than assuming this guarantee.

## Troubleshooting

- **No LED and no wired interface:** confirm the USB-C-to-Micro-USB cable carries data, that the Philhower **Arduino Nano RP2040 Connect** board was selected, and that **USB Stack: Pico SDK** was selected.
- **LED on but Safari cannot connect:** turn off VPN software temporarily, keep the phone unlocked, wait for DHCP, and enter the numeric HTTP URL exactly.
- **Ethernet appears but Automatic IP remains blank:** leave the Nano connected,
  switch Configure IP to Manual and then back to Automatic to trigger another
  DHCP request. If that works but a fresh connection does not, report the iPad
  model and iPadOS version; the next step is deferred USB connection support in
  the NCM library rather than another forced re-enumeration.
- **Safari upgrades to HTTPS:** type the complete `http://192.168.7.1/` URL again or use a fresh private tab.
- **The phone reports no Internet:** expected. This is an isolated device network. Cellular or Wi-Fi should remain available through a separate iOS route, but behavior should be qualified on target OS versions.
- **Upload no longer works:** double-tap the Nano's RESET button to expose the `RPI-RP2` bootloader drive, then upload again.
- **Wi-Fi/NINA behavior:** this proof of concept deliberately does not initialize the onboard NINA-W102. Do not add `WiFiNINA` initialization while testing USB NCM; it would introduce a separate network path and make the result ambiguous.

## Scope and caveats

- This is proof-of-concept firmware, not a production security design.
- The HTTP and TCP services are unencrypted and unauthenticated.
- The DHCP API is internal to Arduino-Pico and may change between core releases.
- The sketch calls lwIP initialization explicitly so the DHCP listener can be
  opened before `ethernet.begin()` exposes NCM to the host. Arduino-Pico's lwIP
  initialization wrapper is idempotent in the targeted 6.0.0 core.
- The sketch passes explicit mutable `ip_addr_t` structures to that DHCP API;
  this avoids the const/ambiguous `IPAddress` conversion error in Arduino-Pico 6.0.0.
- USB NCM behavior must be tested across every supported iPhone/iPad and iOS/iPadOS release.
- RP2040 USB is full-speed (12 Mbit/s nominal); this test intentionally sends only about 5.4 KB/s per stream.
