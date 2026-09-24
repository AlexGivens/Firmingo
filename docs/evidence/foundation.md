# Foundation implementation evidence

Recorded 2026-09-15 (America/Los_Angeles), macOS 27.0 build 26A428, Apple Silicon.
No board was selected or contacted. No firmware was uploaded.

## Changes and purpose

- Preserved the imported sketch byte-for-byte; SHA-256:
  `4887fd480e98025392a85751138127410ad0b314a6ea51c7f18bdd1d1cc3c209`.
- Added explicit Arduino CLI 1.5.1 / Arduino-Pico 6.0.0 build settings and memory
  budgets, plus the upstream package checksum and bundled tool versions.
- Added `src/core/stream.*`: portable full-duplex byte pump with bounded pending
  data, partial-write retention, one owner, disconnect cleanup and idle timeout.
  The existing board sketch does **not** use this component yet.
- Added Unity 2.6.1 / CMake / CTest native tests and a sockets/pytest smoke harness.
  This makes binary integrity and failure behavior testable before transport work.
- Added doctor/test/build/smoke entry points, setup/testing docs and PR CI
  configuration. The GitHub workflow has not been executed in this session.

## Commands actually run

The `.venv` was created using the desktop runtime's Python 3.12.14 because the
default installed Python could not run. Dependencies installed in this repository
only: CMake 3.31.6, pytest 8.3.5, iniconfig 2.3.0, packaging 26.3, pluggy 1.6.0.
No global Python packages, compiler selection or Boards Manager sources changed.

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
CC=/Library/Developer/CommandLineTools/usr/bin/clang \
CXX=/Library/Developer/CommandLineTools/usr/bin/clang++ \
.venv/bin/python tools/dev.py test --sanitize
```

Result: **pass**, eight Unity cases (one CTest executable), address and undefined
behavior sanitizers enabled; **22 pytest host-harness tests passed**. Native tests
include all 256 write chunk sizes across binary full-duplex data, stalled writers,
ownership rejection, queue disposal/reconnect, exact timeout boundaries/clock
rollover, faulty adapter counts and empty I/O. Host tests include corruption,
fragmentation/coalescing, total deadlines and a real localhost socket exchange.
Initial sandbox execution passed native checks and 19 host cases but denied the
three localhost listeners; the full command subsequently passed with localhost
access. This is harness evidence, not hardware evidence.

```sh
.venv/bin/python -m pytest tests/hardware -q
```

Result: **3 skipped**, because no board/address was selected. A call to smoke
with board/address but without `--allow-legacy-no-identity` failed before opening
sockets, as intended. `smoke --help` displayed explicit required target arguments.

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
CXX=/Library/Developer/CommandLineTools/usr/bin/clang++ \
ARDUINO_CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' \
.venv/bin/python tools/dev.py doctor
```

Result: version probes and baseline checksum pass (AppleClang 21.0.0, CMake/CTest
3.31.6, pytest 8.3.5, Arduino CLI 1.5.1, core 6.0.0). Doctor explicitly does not
claim that all Arduino helper binaries execute or that hardware is available.

```sh
ARDUINO_CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' \
.venv/bin/python tools/dev.py build --board nano_rp2040_connect
```

Result: **failed** before firmware compilation. Arduino selected the intended
core and libraries but its installed helper failed:

```text
Error during build: fork/exec .../builtin/tools/ctags/5.8-arduino11/ctags: bad CPU type in executable
```

`file` identified that helper as Mach-O x86_64; the RP2040 cross-compiler is arm64.
An earlier direct CLI build attempt failed at the same helper. Generated local
logs/report are in `build/firmware/nano_rp2040_connect/`. No compiled image hash,
flash usage, static RAM result or compiler-warning result is available. The
sketch hash identifies source only. Installed core contents have not been
compared with a clean upstream archive; clean installation remains required for
reproducible release evidence.

## Retry after Xcode and Command Line Tools reinstall

The default Xcode selection now works without the earlier license error or
`DEVELOPER_DIR`/compiler overrides. `git status`, `clang++ --version` and
`python3 --version` succeed; default Python reports 3.9.6, so continue using the
project's Python 3.12 virtual environment. System `/usr/local/bin/cmake` remains
an incompatible x86_64 executable; the pinned virtual-environment CMake works.

Commands run again:

```sh
ARDUINO_CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' .venv/bin/python tools/dev.py doctor
.venv/bin/cmake --fresh -S . -B build/native-sanitize -DCMAKE_BUILD_TYPE=Debug -DFIRMINGO_SANITIZE=ON
.venv/bin/python tools/dev.py test --sanitize
ARDUINO_CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' .venv/bin/python tools/dev.py build --board nano_rp2040_connect
```

Fresh CMake configuration selected the default Xcode toolchain, not the previous
cached Command Line Tools compiler. Doctor passed its version/checksum probes.
The rebuilt eight native sanitizer cases and 22 host tests passed again.
The firmware attempt still failed at Arduino's x86_64 `ctags` 5.8-arduino11 with
`bad CPU type in executable`. Direct execution outside the sandbox failed with
the same error, so this is not a sandbox-only restriction. Reinstalling Xcode
has resolved the Xcode tool access issue but not this Arduino helper's CPU
compatibility. No board was contacted or flashed during the retry.

## Successful build after Rosetta 2 installation — 2026-09-16

Direct `ctags --version` now succeeds (Exuberant Ctags Development, compiled
June 14, 2019). The following command exited 0 without changes to the sketch,
board core, or Arduino helper:

```sh
ARDUINO_CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' .venv/bin/python tools/dev.py build --board nano_rp2040_connect
```

- Arduino CLI 1.5.1; Arduino-Pico 6.0.0; board options unchanged in the manifest.
- Program storage reported by the compiler: **113,716 bytes**.
- Static RAM: **75,048 bytes**; reported remaining RAM: 187,096 bytes. Runtime
  stack/heap high-water use remains unmeasured.
- Both configured memory budgets passed. Flash layout remains 16MB, no FS.
- 22 warning occurrences, all `WiFiClient::write(uint8_t)` hidden overloads
  (`-Woverloaded-virtual`) in the installed Arduino-Pico WiFi headers.
- No sketch-source warnings were present in this build log.

Artifact SHA-256 values from the successful build report:

| Artifact | File bytes | SHA-256 |
| --- | ---: | --- |
| `RP2040_USB_NCM_POC.ino.bin` | 131032 | `aadb1df697dcbb29b141678f0ec39c3dcf4d073870386ae91058f94392c8427e` |
| `RP2040_USB_NCM_POC.ino.uf2` | 262144 | `826d811dde1024b2e33b30f1efc7973db7ebad3c2051f8273979c0d7e8b3bcbd` |
| `RP2040_USB_NCM_POC.ino.elf` | 2688936 | `862944e62b1cb4394dca753656aa07b6554aa06a9c2260a6ca40ff3aff1bf958` |
| `RP2040_USB_NCM_POC.ino.map` | 1793482 | `5855e8055f1b774f41a4f86e28fa619d37d2697097f2a1214674426ea2725b28` |

Raw BIN file size differs from the compiler's program-storage accounting; both
are recorded above. Generated files, build log and report are under
`build/firmware/nano_rp2040_connect/`. This establishes a local baseline compile
pass, not hardware qualification or a clean-install reproducibility claim.
No board was contacted or flashed. Native tests were not rerun in this retry:
no firmware/core/test source changed, and the preceding Xcode retry passed them.

## Remaining limits and next work

Hardware preflight on 2026-09-16: Arduino CLI detected no Nano, the USB inventory
did not show a Nano, and no active USB NCM network interface was identified.
`route -n get 192.168.7.1` selected Wi-Fi `en0` on `192.168.4.0/22`, which overlaps
the baseline's subnet. Route/hardware-port inspection was repeated outside the
sandbox after the sandbox denied those read-only queries. No test sockets were
opened, and no reset, upload or network configuration change was performed.
Hardware smoke remains unrun pending physical connection and confirmation of
the installed firmware/address. Recheck the route after the board is attached.

After the user connected the Nano, Arduino CLI detected `Nano RP2040 Connect`
at `/dev/cu.usbmodem101` (VID 0x2341, PID 0x005E). USB interface inspection showed
CDC ACM control (class 2, subclass 2, protocol 1) and its data interface (class 10,
subclass 0, protocol 0), but no NCM interface or new USB Ethernet network port.
The route to `192.168.7.1` still selected Wi-Fi `en0`. This establishes USB serial
enumeration only; the installed firmware and reason for absent NCM are unknown.
Asked the user which firmware is installed. No serial port was opened, test
payload sent, reset issued, or image uploaded.

The Arduino helper compatibility issue is resolved and the baseline build passes.
Next, run exact-byte smoke on an explicitly selected board. Integrate the portable
component with a nonblocking board adapter after recording baseline behavior.
Do not directly treat the Arduino WiFiClient blocking API as this adapter contract.

The prototype still ignores short writes, replaces existing clients, advertises
router/DNS through DHCP and has unresolved iOS cold attachment. The LED and page
counter do not establish exact integrity or automatic addressing. Local-only
DHCP, pre-sketch USB startup, stable identity, framed control, device diagnostics,
UART support, firmware updates and second-board support remain outstanding.
No Swift interoperability, iOS authorization, Internet coexistence, throughput
threshold or production-readiness claim follows from these tests.

Follow-up: the original baseline was subsequently flashed, exposed NCM and
passed its source test, but echo failed and Internet access was interrupted.
The local-only replacement now passes one macOS source/Internet coexistence
session. See [the newer report](local-only.md) for actual results and limits.
