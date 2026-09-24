# Board support and qualification

The current source version is **0.1.0 beta**. The release manifest records exact
board IDs. A successful compile is not an attachment, routing, byte-integrity,
or iOS pass. The older hardware results linked below apply to named pre-0.1.0
images; they do not automatically qualify newly built UF2s.

| Board ID | Build configuration | Earlier hardware evidence | 0.1.0 release status |
| --- | --- | --- | --- |
| `nano_rp2040_connect` | Arduino-Pico 6.0.0, Pico SDK USB, 16 MiB no FS | [Application](evidence/application-hardware.md), [UART](evidence/uart.md), [iPad local-only](evidence/ios-qualification.md) | Needs exact-image smoke and iOS checks |
| `raspberry_pi_pico` | Arduino-Pico 6.0.0, Pico SDK USB, 2 MiB no FS | [Local-only and application](evidence/raspberry-pi-pico.md); UART hardware untested | Needs exact-image smoke and iOS checks |
| Other boards | No declared build port | None | Unsupported |

Pinned FQBNs, core/CLI versions, memory budgets, and per-port capabilities are in
[`boards/nano_rp2040_connect/baseline.json`](../boards/nano_rp2040_connect/baseline.json)
and [`boards/raspberry_pi_pico/baseline.json`](../boards/raspberry_pi_pico/baseline.json).
The first two board IDs use different UF2 images and flash layouts. A shared
RP2040 chip is not proof of image interchangeability. Select the artifact by
exact board ID, profile, version, size, and SHA-256 from the generated
[release manifest](releasing.md).

The Nano's NINA Wi-Fi module is unused by these USB profiles. `application`
is the normal byte-stream reference; `uart` binds channel 1 to GPIO 0/1;
`local-only` exists for DHCP/attachment qualification. The preserved predecessor
sketch advertises router/DNS options and is not a release profile. No profile
implements network firmware updates or production authentication.
