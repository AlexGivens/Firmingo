# Raspberry Pi Pico UART peer fixture

This test-only image turns an original Raspberry Pi Pico into an independent
3.3 V UART peer for the Nano RP2040 Connect. It is not Firmingo product firmware.
USB CDC accepts bounded text commands; UART0 uses GP0/TX and GP1/RX.

Wire only after flashing, with both boards unpowered:

| Nano RP2040 Connect | Raspberry Pi Pico |
| --- | --- |
| TX / GPIO0 | GP1 / UART0 RX, physical pin 2 |
| RX / GPIO1 | GP0 / UART0 TX, physical pin 1 |
| GND | GND, physical pin 3 |

Power both boards through their own USB cables. Do not connect their power rails,
and do not leave UART wiring attached while only one board is powered.

Build and flash through the explicit test-fixture tool:

```sh
.venv/bin/python tools/pico_peer.py build
.venv/bin/python tools/pico_peer.py flash --mount /Volumes/RPI-RP2
```

The `pico-uart-peer-v2` image starts UART0 at 115200 8N1 with a 256-byte receive
FIFO. USB CDC
commands are newline terminated:

- `PING`, `STATS`, and `CLEAR`
- `BAUD <300..2000000>`
- `CONFIG <baud> <5..8 data bits> <none|even|odd> <1|2 stop bits>`
- `MODE ECHO` or `MODE SINK`
- `EXPECT <count> <seed>` consumes and validates deterministic UART input
- `SEND <count> <seed>` transmits the same deterministic byte generator

All storage is fixed. Counters and first mismatch information are reported over
USB CDC and never enter the UART data path. For 5-, 6-, and 7-bit formats, the
deterministic generator masks values to the configured data width so comparisons
remain exact without claiming preservation of bits that the UART does not send.

With both boards powered and wired, run the independent-direction and controlled
overrun checks explicitly:

```sh
.venv/bin/python tools/uart_peer_smoke.py \
  --address 192.168.77.1 --device-id <NANO-ID> \
  --board nano_rp2040_connect --peer-port /dev/cu.usbmodemXXXX
.venv/bin/python tools/uart_overrun.py \
  --address 192.168.77.1 --device-id <NANO-ID> \
  --board nano_rp2040_connect --peer-port /dev/cu.usbmodemXXXX \
  --output build/uart-overrun.json
.venv/bin/python tools/uart_format_matrix.py \
  --address 192.168.77.1 --device-id <NANO-ID> \
  --board nano_rp2040_connect --peer-port /dev/cu.usbmodemXXXX \
  --output build/uart-format-matrix.json
```

The original Nano/Pico run passed 8,192 exact bytes in each direction at 115200 and
4,096 exact bytes in each direction at 57600. The controlled 4 KiB unsolicited
burst filled the Nano's 256-byte FIFO, reported one conservative minimum lost
byte, drained on acquisition, and recovered with 4,096 exact bytes each way. The
format-matrix command independently checks every advertised data/parity/stop
shape, the minimum and maximum baud rates, invalid-setting rejection, exact
traffic in both directions, and unchanged error counters. See
[the UART evidence record](../../../docs/evidence/uart.md) for observed results.
