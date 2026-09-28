# M7 experimental Nano resident: first ROM image

2026-09-24, macOS 27.0 (26A428). The user identified and placed a Nano RP2040
Connect into ROM recovery using REC/GND. `/Volumes/RPI-RP2/INFO_UF2.TXT`
reported UF2 Bootloader v2.0 and generic `Board-ID: RPI-RP2`; that ROM ID does
not independently identify a Nano. Board revision, cable, and hub were not
recorded. This was an experimental image, outside the 0.1.0 release package.

| Artifact | Exact result |
| --- | --- |
| Resident core/build | Arduino-Pico 6.0.0 deferred-start NCM-only overlay, CLI 1.5.1 |
| Resident reported size | 128,048 flash bytes; 88,768 static RAM bytes |
| Resident UF2 SHA-256 | `056506069bcf6784aaed5d7ba5543a6cf728a7ecd68a0174e55dc7c5816f827d` |
| Blink A capsule | 432 code bytes; SHA-256 `702dc1a8c5645024eaebfcefd843fa869c5dccab29ea8520732a2cf15646ee45` |
| Blink A composite UF2 | 294,400 file bytes; SHA-256 `81e8cdaacdc23b7ebc688d5eb7718ea1249d1070ea90f9a95f06b8295a3d372d` |

The exact-hash command checked the current sources, build reports, image
format, UF2 address bounds, mounted ROM volume, and selected SHA-256, then
copied the image:

```sh
.venv/bin/python experiments/managed_sketch/flash_initial.py \
  --board nano_rp2040_connect --sketch blink_a --mount /Volumes/RPI-RP2 \
  --expected-uf2-sha256 81e8cdaacdc23b7ebc688d5eb7718ea1249d1070ea90f9a95f06b8295a3d372d
```

The ROM volume disappeared. This is a host copy record, not a flash read-back.

After boot, macOS `en5` was active at `192.168.77.16/24`. The route to
`192.168.77.1` used `en5`; the default route still used Wi-Fi `en0`. This one
route snapshot does not establish uncached Internet coexistence. FMGO hello at
the board address reported `board_id:nano_rp2040_connect`, experimental
`firmware_version:0.1.0-m7exp1`, an observed 16-hex device ID, and one boot ID.
The IDs are redacted here under the evidence directory's public-transcript
policy. The distinct firmware marker distinguishes this resident from the
ordinary 0.1.0 application, but the device cannot attest its own flash hash.

The nondestructive command below used the observed device ID and passed these
exact-byte checks in one boot (the public transcript redacts the ID):

```sh
.venv/bin/python experiments/managed_sketch/smoke.py --address 192.168.77.1 \
  --device-id REDACTED_16_HEX_ID --sketch blink_a
```

| Check | Observed |
| --- | --- |
| Binary echo | 16,384/16,384 matching bytes, 125 fragmented sends, 0.826 s |
| Delayed reader | 4,096/4,096 matching bytes, 0.491 s, 150 ms initial read delay |
| Second client while owned | Explicit `busy` response |
| Ownership transfer | 4,096/4,096 matching bytes |
| Fresh TCP session | 4,096/4,096 matching bytes; same boot ID |
| Byte counters after first two echoes | 20,480 received and 20,480 sent |

One diagnostic snapshot after this traffic reported 172,924 free C-heap bytes
and 3,952 free core-0 stack bytes, with the same observed minima, zero
application RX/TX discarded bytes, and zero TCP errors. The diagnostic sample
count was 227,509. It did not measure core-1 stack, lwIP's fixed pool, or
transient worst cases. The reported throughput values are single-run
observations, not performance thresholds.

The user then observed the built-in D13 LED blinking at roughly 250 ms on and
250 ms off, matching Blink A's programmed half-period. Together with the exact
UF2 copy, the experimental resident identity, and the binary echo, this
supports that Blink A executed on core 1. The observation is visual rather
than a flash read-back or independent capsule attestation. No network replacement,
staging, authorization, cold reconnect series, deliberately stalled sketch,
power-cut test, or iPhone/iPad check has been performed for this image. M7
acceptance and the 0.1.0 exact-image release checks remain open.

## One normal unplug/replug

The user unplugged the Nano, waited five seconds, and reconnected it without
REC/GND. They again observed D13 blinking at roughly 250 ms intervals. macOS
returned `en5` to `192.168.77.16/24`; `RPI-RP2` did not remain mounted. FMGO
hello reported the same device ID and experimental firmware marker but a
different boot ID, confirming a new firmware boot. The local board route used
`en5`; the default route still used Wi-Fi `en0`.

The same nondestructive experimental smoke passed on that new boot: 16,384
exact binary bytes, 4,096 with a delayed reader, explicit second-client busy,
4,096 after ownership transfer, and 4,096 after a fresh TCP session. The first
echo took 0.635 s, and the delayed-reader echo took 0.432 s. The diagnostic
snapshot reported 172,924 free C-heap bytes, 3,952 free core-0 stack bytes,
zero application RX/TX discarded bytes, and zero TCP errors. These are sampled
values, not memory-stability or throughput guarantees. This is **one** observed
normal reconnect, not the cold-attachment series or an iOS result.
