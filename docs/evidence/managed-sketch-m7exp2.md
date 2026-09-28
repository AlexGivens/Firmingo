# M7 Nano `m7exp2`: first ROM boot and failed Blink A echo

2026-09-24, macOS 27.0 (26A428), the same operator-selected Nano RP2040
Connect as the [first experiment](managed-sketch-initial.md). The user mounted
its generic `RPI-RP2` ROM volume with REC/GND. Board revision, cable, and hub
were not recorded. This remains an experimental image outside the 0.1.0
release package.

| Artifact | Exact build result |
| --- | --- |
| Resident core/build | Arduino-Pico 6.0.0 deferred-start NCM-only overlay, CLI 1.5.1 |
| Resident size | 131,752 flash bytes; 93,472 static RAM bytes |
| Resident UF2 SHA-256 | `931c39df5cbb4b85963daefc1be58793e3d0a8c462f876f1965b382d8f8f0bc4` |
| Blink A capsule | 432 code bytes; SHA-256 `68516001acf84205a9af84e497aae598ba3be1c192188da5c1fc32ca361d609d` |
| Blink A ROM composite | 302,080 file bytes; SHA-256 `da838d4523ee86307de9f1729395a350b805cabb8cb9bd6b23ace35775c0a7a0` |

The exact-hash `flash_initial.py` command regenerated and verified the
composite and copied it to `/Volumes/RPI-RP2`. The volume then disappeared;
this records a host copy, not a flash readback. macOS `en5` was active at
`192.168.77.16/24`. FMGO hello at `192.168.77.1` reported the same observed
device ID as the prior Nano record, a new boot ID, board ID
`nano_rp2040_connect`, and `firmware_version:0.1.0-m7exp2`. Exact IDs are
redacted here under the evidence directory's public-transcript policy.

The nondestructive `smoke.py` check selected `m7exp2` and Blink A. It failed
on the first binary echo: the host sent all 16,544 framed wire bytes for a
16,384-byte payload and received **zero** echo bytes before the socket timed
out. The expected result was 16,384 matching bytes. The remaining smoke
checks did not run. FMGO hello and `serial.open` continued to work, but this
does not show that the sketch slot validated or that Blink A executed.

A separate diagnostic read while three application bytes were sent reported
zero application TX peak, zero TCP errors, 168,220 free C-heap bytes, and
3,920 free core-0 stack bytes. It showed an application RX peak of 256 and
256 discarded RX bytes after the failed smoke connection closed. These
sampled counters do not distinguish a rejected slot from a core-1 scheduler
fault. The user's D13 observation is pending. No network replacement or flash
write from the running firmware has been attempted. The new resident's M7
qualification remains failed/incomplete until this is diagnosed and retested.

After this boot, inspection of the pinned Nano variant found that physical D2
is GPIO25, while the installed resident uses numeric GPIO2 for its upload-arm
input. The operator was told to leave D2 and GND unconnected. The source now
uses the named `D2` constant with a compile-time mapping check and distinct
`m7exp3` identity. Its corrected resident compiled at the same size and has
UF2 SHA-256
`3986a88b7fbe57fad389304dcd66ade38b59a7f015e3947472d3bafb71af5eb7`.
The corrected Blink A ROM composite has SHA-256
`c2e48dafe8555c1ef1f69b3c177c97de3078ce21657c057f9804c0dfaeb40a40`.
Neither corrected artifact has been installed or tested on hardware. This pin
mapping error explains why a physical D2 jumper would not arm the installed
upload listener; it does not by itself explain the failed Blink A echo.
