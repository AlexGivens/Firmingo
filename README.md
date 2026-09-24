# Firmingo

**Version 0.1.0 beta.** Firmingo is RP2040 firmware that exposes a local USB
CDC-NCM Ethernet connection and a bounded, ordered binary stream. It is an
experimental developer release: the protocol is not frozen, the development
profile has no application authentication, and firmware update over the network
is not implemented.

The repository currently builds for the **Arduino Nano RP2040 Connect** and
**Raspberry Pi Pico** using the pinned Arduino-Pico core. An application-stream
reference and a hardware-UART bridge share the portable FMGO session code.
A separate local-only diagnostic sketch and an untouched predecessor prototype
remain available for qualification. Exact 0.1.0 images need fresh hardware and
iOS checks before any hardware-verified release claim. Earlier hardware results
are indexed in [evidence](docs/evidence/README.md); they apply to the specific
older images named there.

## Get oriented

| Path | Purpose |
| --- | --- |
| [src/core](src/core) | Portable stream, framing, session, and release version source |
| [src/ports/arduino_pico](src/ports/arduino_pico) | Arduino-Pico USB/IP/UART adapters |
| [boards](boards) | Exact board build settings, memory budgets, and capabilities |
| [examples/firmingo_application](examples/firmingo_application) | Application byte-stream reference; normal release profile |
| [examples/firmingo_uart](examples/firmingo_uart) | Hardware-UART bridge reference |
| [examples/firmingo_local_only](examples/firmingo_local_only) | NCM/DHCP/traffic qualification image |
| [examples/rp2040_usb_ncm_poc](examples/rp2040_usb_ncm_poc) | Checksummed original proof of concept |
| [tests](tests) and [tools](tools) | Native, host, hardware, build, and packaging checks |
| [docs](docs/README.md) | Design, setup, release, and indexed evidence |
| [dist](dist/README.md) | Generated, board-specific release packages (Git-ignored) |

Build and test with Python 3.10+, Arduino CLI 1.5.1, Arduino-Pico 6.0.0,
and the pinned dependencies in `requirements-dev.txt`:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-dev.txt
.venv/bin/python tools/dev.py doctor
.venv/bin/python tools/dev.py test --sanitize
.venv/bin/python tools/dev.py build --board nano_rp2040_connect --firmware application
.venv/bin/python tools/dev.py build --board raspberry_pi_pico --firmware application
.venv/bin/python tools/dev.py build --board nano_rp2040_connect --firmware uart
.venv/bin/python tools/dev.py build --board raspberry_pi_pico --firmware uart
.venv/bin/python tools/dev.py package
```

The last command stages `dist/firmingo-0.1.0/` with a separate UF2 for each
board and profile, plus a JSON manifest containing its exact board ID, version, byte size,
SHA-256, profile, and build settings. **Select by exact board ID.** These are
compile-tested artifacts until the exact hashes pass [release qualification](docs/releasing.md).
The matching ZIP retains the versioned directory for a GitHub release asset.
The package is local; creating it does not publish a GitHub release or flash a
board. See [development setup](docs/development.md) for CLI configuration and
[releasing](docs/releasing.md) for artifact checks and publication.

## Scope and status

The application profile offers a provisional FMGO v1 stream on TCP 7420 with
one owner, bounded queues, explicit control frames, and application-owned bytes.
The UART profile bridges the same transport to GPIO 0/1 at a selected supported
baud rate; its lack of CTS means an external transmitter can overrun its fixed
receive FIFO. Local-only DHCP omits router and DNS options so the device does not
advertise itself as an Internet gateway. The original prototype still advertises
a gateway and must not be mistaken for the release profile.

The [protocol contract](docs/protocol.md) separates implemented behavior from
proposed programming and security capabilities. [Board status](docs/boards.md)
separates build support from hardware results. The [work ledger](TODO.md) lists
remaining validation, including FMGO on iOS, repeated attachment, long traffic
runs, and release security design. FirmingoKit, the future Swift SDK, is a
separate project. This repository builds firmware and test tools, not a host IDE
or programming service.

The repository includes the [GNU GPL version 3 license](LICENSE). Vendored
components retain the upstream license and provenance recorded under
[`third_party/`](third_party/).
