# RP2040 hardware-UART bridge reference

Experimental FMGO v1 UART endpoint on `192.168.77.1:7420`. Firmingo **0.1.0 beta**
bridges opaque bytes between the network owner and the Nano RP2040
Connect's exposed hardware UART:

The exact 0.1.0 image is compile-tested until fresh hardware checks are
recorded. The V2 hardware results below refer to the earlier development UF2.

| Signal | Nano pin | RP2040 GPIO |
| --- | --- | --- |
| UART TX | D1 / TX | GPIO 0 |
| UART RX | D0 / RX | GPIO 1 |
| Ground | GND | — |

The same source is compile-tested for the Raspberry Pi Pico, where UART0 uses
GP0/TX and GP1/RX. Direct Pico UART/NCM hardware behavior remains unverified.

The default is 115200 baud, 8 data bits, no parity, one stop bit, and no hardware
flow control. `serial.open` can supply a complete `config` object; an owner can
also use `serial.configure` while portable Stream queues are empty. Supported
baud requests are 300 through 2,000,000; the reply reports the actual hardware
baud. Data bits 5–8, parity `none`/`even`/`odd`, and one or two stop bits are
supported. Break, DTR, RTS, CTS, and software flow control are not offered.
Configuration and new ownership return `busy` while the hardware transmitter is
active. A successful configuration discards and counts unread receive bytes before
restarting the UART; the external transmitter must be paused during that change.

```sh
.venv/bin/python tools/dev.py build --board nano_rp2040_connect --firmware uart
.venv/bin/python tools/dev.py flash --board nano_rp2040_connect --firmware uart --mount /Volumes/RPI-RP2
.venv/bin/python tools/dev.py smoke --board nano_rp2040_connect --firmware uart --address 192.168.77.1 --device-id a1b2c3d4e5f60718
.venv/bin/python tools/dev.py soak --board nano_rp2040_connect --firmware uart --address 192.168.77.1 --device-id a1b2c3d4e5f60718 --firmware-sha256 <UF2-SHA256> --output build/uart-soak.json --diagnostics
```

Build and flash preserve the same pinned Arduino-Pico 6.0.0 NCM-only/local-DHCP
settings as the application reference. The UART smoke command additionally
requires a jumper from D1/TX to D0/RX; it validates the reported UART capability,
configuration acknowledgement, and exact looped-back bytes. Remove external
circuits before installing the jumper, and share ground when testing with another
device. The Nano uses 3.3 V logic.

The Arduino-Pico receive FIFO is fixed at 256 bytes. Portable Stream queues add
256 bytes per direction. V2 reserves 64 receive-FIFO bytes before accepting more
locally originated TX, so an echoing/responding target applies backpressure when
network output stalls. `uart_tx_throttles` counts those write attempts. A remote
UART transmitter cannot be stopped because this profile exposes no CTS, so it can
still overrun the bounded FIFO. Diagnostics report `uart_rx_overrun_events` and
`uart_rx_lost_bytes_minimum`; each latched hardware overrun proves at least one
lost byte, though the exact loss can be larger. Close, disconnect, and new-owner
acquisition discard unread UART receive bytes and report the count. Bytes already
placed in the UART transmitter cannot be recalled; a new owner is held off until
they drain.

V1's first bounded macOS loopback, repeat from persisted 57600 configuration, and
concurrent Wi-Fi Internet check passed. Its first measured soak then exposed a
real late RX overrun after 393,216 exact bytes. V2 adds the local-TX receive
reserve and throttle diagnostic as the focused correction. On the same Nano and
D1-to-D0 loopback, V2 passes smoke and the unretried 60-second replay with 458,752
exact bytes across seven sessions. Throttling was exercised while overrun/loss,
discarded bytes, and TCP errors remained zero; Wi-Fi Internet stayed usable. See
[the evidence record](../../docs/evidence/uart.md). An independent external UART
peer now passes 8,192 exact bytes in each direction at 115200 and 4,096 each way
at 57600. A controlled unsolicited 4 KiB burst filled the 256-byte FIFO, reported
one conservative minimum lost byte, drained on acquisition, and recovered with
4,096 exact bytes each way. The v2 peer fixture passes all 24 advertised
data/parity/stop-bit combinations at 57600, plus bounded exact transfers at 300
and 2,000,000 baud. A separate uninterrupted 1,024-byte peer burst at 2 Mbaud
overran the Nano RX FIFO, as expected without CTS. iOS and cold-attachment rates
remain unverified. This profile does not provide a runtime console, USB CDC
serial, firmware updater, or authentication beyond the open-development profile.
