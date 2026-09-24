# Historical product and protocol notes

This is the pre-0.1.0 README retained for research. It mixes implemented behavior, historical test reports, and proposals. For current release status use the repository README, docs/boards.md, docs/protocol.md, and docs/evidence/README.md. Historical firmware names and test claims below refer to earlier images.

# Firmingo

Microcontroller firmware for fast serial communication and firmware programming
over Ethernet, designed for apps on macOS, iPadOS, and iOS.

The long-term goal is coverage of all common hobbyist microcontrollers through
qualified direct board ports and bridge/programmer firmware. This is a roadmap,
not a claim of universal compatibility or a single firmware binary for every board.

**Status:** preserved Nano RP2040 Connect proof of concept, a portable bounded
stream component integrated into the qualification echo endpoint, portable FMGO
framing/JSON sessions with bounded application and hardware-UART backends, a
bounded TCP adapter, Nano reference applications, shared wire fixtures, native tests,
and build/flash/smoke/opt-in traffic-soak tooling. The same reference profiles now
compile for a Raspberry Pi Pico as a second explicit board configuration. Its
local-only image passes one macOS BOOTSEL/attachment/DHCP/exact-traffic/Internet-
coexistence session; application v7 also passes full FMGO smoke, a 622,592-byte
one-minute diagnostics soak, and exact-transfer Internet coexistence. Pico UART remains
compile-tested only. The
preserved baseline remains unchanged.
See [foundation results](../../docs/evidence/foundation.md) and
[first sustained-traffic results](../../docs/evidence/application-soak.md).
The production protocol below is **draft and not frozen**. Its header codec is
implemented alongside JSON validation and application session handling. A
Nano application v7 reference compiles with the TCP listener and now passes
macOS binary/control/ownership/reconnect smoke plus a 64 KiB exact transfer
with concurrent Wi-Fi Internet access. Explicit lwIP timer servicing corrected
the earlier stalled transfer/reconnect checks. See
[application test evidence](../../docs/evidence/application-hardware.md).
The [development contract](../../docs/protocol.md) distinguishes implemented framing
from proposed semantics for the separate **FirmingoKit** Swift SDK. The [application reference](../../examples/firmingo_application/README.md) accepts this
protocol on provisional TCP 7420. A separate [UART reference](../../examples/firmingo_uart/README.md)
bridges that protocol to Nano pins D1/TX and D0/RX. UART v1 passed initial bounded
macOS loopback/coexistence checks, then exposed RX loss late in its first measured
soak. UART v2 adds focused local-TX backpressure and passes the same unretried
macOS hardware replay: 458,752 exact bytes, seven sessions, exercised throttling,
zero UART overrun/loss, and concurrent Wi-Fi Internet access. A separately
programmed Raspberry Pi Pico also passes exact traffic in each direction at
115200 and 57600, plus a controlled overrun/recovery check. See
[UART build and hardware evidence](../../docs/evidence/uart.md).
The preserved old sketch and separate qualification
image do not accept the protocol.

The original prototype has now been tested on the Mac: its DHCP configuration
interrupted Internet access, and the large echo test timed out. A separate
**local-only qualification firmware** is implemented and has passed one macOS
post-flash source-plus-Internet coexistence check and exact binary echo checks
through 256 KiB. See [echo evidence and limits](../../docs/evidence/echo.md). All three
staged local-only iPad attachment scenarios now pass, including exact browser
records and Wi-Fi Internet coexistence; FMGO itself remains untested on iOS.
Controlled NCM-only USB startup is implemented and
flashed; its first diagnostic request timed out before the host driver eventually
bound. Later byte-integrity/Internet checks pass. Three physical macOS reconnections
pass, with cycle 2 networking ready 2.864 s after host-observed USB appearance.
The full 20-connection attachment target remains incomplete.
See [cold-cycle evidence](../../docs/evidence/macos-cold.md).
See [iPad qualification evidence](../../docs/evidence/ios-qualification.md).
See [USB startup evidence](../../docs/evidence/usb-startup.md) and
[replacement and validation steps](../../docs/local-only-network.md)
and [recorded results](../../docs/evidence/local-only.md).

## Start development

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-dev.txt
.venv/bin/python tools/dev.py doctor
.venv/bin/python tools/dev.py test --sanitize
.venv/bin/python tools/dev.py build --board nano_rp2040_connect
.venv/bin/python tools/dev.py build --board raspberry_pi_pico --firmware local-only
```

Use Python 3.10+, a native compiler, Arduino CLI 1.5.1 and Arduino-Pico 6.0.0.
[Development setup](../../docs/development.md) documents tool selection, exact board
settings, the Rosetta requirement for the installed ctags helper, and portable adapter semantics.
[Pico port evidence](../../docs/evidence/raspberry-pi-pico.md) records its pinned
configuration, compile results, recovery path, and remaining hardware checks.
[Timed macOS attachment checks](../../docs/cold-attachment.md) observe physical reconnects
and record host-observed readiness. [Hardware testing](../../docs/testing.md) provides explicit-target byte-integrity smoke
commands and an iOS checklist. [Board status](../../docs/boards.md) distinguishes
planned builds from hardware evidence. The [original example](../../examples/rp2040_usb_ncm_poc/)
remains unchanged in behavior; its DHCP still advertises router/DNS, and its echo
loop does not yet handle partial writes reliably.

## Project boundaries

| Component | Responsibility |
| --- | --- |
| Firmingo (this repository) | MCU firmware, network/serial/programming adapters, wire contract, firmware tests |
| FirmingoKit (separate Swift project) | Discovery, connection state, wire encoding/decoding, serial and programming APIs for Apple apps |
| Host development app | Editing, compilation or obtaining images, UI, permissions explanation, device selection and user intent |

A small Python test harness belongs here to validate firmware. A Swift SDK,
compiler, IDE, or general web editor does not.

Read [AGENTS.md](../../AGENTS.md) for engineering rules and [TODO.md](../../TODO.md) for
priorities and evidence. [Protocol development contract](../../docs/protocol.md) and
shared vectors in `tests/fixtures/protocol-v1/` cover framing, hello and rejected
reset. Application session schemas are implemented and native-tested;
board/Swift interoperability remains incomplete.

## Connection and board modes

**Direct:** the target MCU runs Firmingo, presents USB Ethernet, and exposes its
application stream/UART and a supported self-update mechanism. Initial installation
still needs a supported bootloader or programmer. New application firmware must
retain Firmingo, or an independent resident service must provide it, to preserve
network connectivity after programming.

**Bridge/programmer:** another MCU runs Firmingo and connects to the target via a
supported UART bootloader, SWD, SPI/ISP, UPDI, or other explicitly implemented
adapter. Those are candidate adapter types, not implemented support claims.
Programming a separate target can preserve the bridge's network connection.
Document target power, voltage levels, reset wiring, recovery, and programming
protocol per supported combination. Serial tunneling alone is not a universal
programmer.

Boards lacking suitable USB hardware/resources need a bridge or another qualified
transport. Firmware on a target cannot turn its fixed USB-to-UART chip into NCM.
Track exact boards/variants, not just family names, with separate build, serial,
flash, and Apple-host verification status.

## What works in the predecessor

The user reported macOS/iPad/iPhone Safari access to a Nano RP2040 Connect using
Philhower Arduino-Pico 6.0.0 and the Pico SDK USB stack. An increasing byte counter
was observed. Latest reported iOS attachment required one board RESET. Byte-perfect
serial interoperability, firmware updates, and automatic cold attachment are not
established by that evidence.

| Prototype interface | Purpose |
| --- | --- |
| `http://192.168.7.1/` | Diagnostic page, HTTP port 80 |
| TCP 5000 | Deterministic test byte source |
| TCP 5001 | Echo test |
| DHCP `.16`–`.23` on `192.168.7.0/24` | Prototype host addresses; confirm imported source |

These endpoints do not implement the draft commands below. Keep any FirmingoKit
prototype adapter explicitly separate from the production protocol adapter.

## Intended fast-connect workflow

1. Connect the cable; complete any legitimate host accessory authorization.
2. Firmware exposes one stable final USB configuration with network services ready.
3. Host obtains a local address. Firmware DHCP omits router and DNS options so it
   does not advertise itself as an Internet gateway; verify coexistence on hardware.
4. FirmingoKit discovers the service, or uses an explicitly selected address.
5. Exchange identity, version, limits, and capabilities; authorize if required.
6. Open a serial channel or a programming session for the selected target.

Draft Bonjour service: `_firmingo._tcp`, with TXT `proto=1` and `id=<device-id>`.
Resolve the actual service address and port; TXT is only a hint, not authenticated
identity. Do not guess target identity from its IP, name, or cached record.
Explicit-address fallback must also perform the handshake. Multi-board addressing
and route conflicts need separate qualification before claiming support.

Measure attach-to-first-verified-byte and each observable stage, reporting median,
p95, maximum, failure count, and sample size. Separate first authorization/user
waiting from automatic startup and reconnection. Numerical budgets remain to be
chosen from measurements. Avoid extra resets, sleeps, and discovery round trips.

## Draft protocol 1: transport and framing

Design choice proposed for review: one TCP connection carrying framed control,
serial data, and programming data. This bounds the number of sockets and keeps
ownership tied to a connection. Use the discovered service port; **7420 is a
provisional development port**, not an allocated service or compatibility promise.
No HTTP, WebSocket, USB CDC serial, or terminal line processing is involved in
this proposed production connection.

All frames have a 16-byte header. Multibyte integers are unsigned, big-endian.

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | Magic: ASCII `FMGO` (`46 4d 47 4f`) |
| 4 | 1 | Protocol major: `1` |
| 5 | 1 | Type: `1` request JSON, `2` response JSON, `3` serial data, `4` upload chunk, `5` event JSON |
| 6 | 2 | Flags: `0` in this draft |
| 8 | 4 | Request ID; nonzero for requests/responses/upload chunks, zero for serial/events |
| 12 | 4 | Payload byte length, excluding header |

JSON payloads are UTF-8 objects without BOM or trailing NUL. Binary types are raw
bytes. A frame can arrive over multiple TCP reads; one read can contain several
frames. Buffer only within negotiated limits. Invalid magic, type, flags, length,
or major version closes the connection; do not scan arbitrary payload for magic.
A syntactically valid request with unsupported semantics receives an error.

Before negotiation, allow payloads up to 4096 bytes. The first request is `hello`.
Both peers advertise `max_payload` between 512 and 65536 bytes; use the smaller
value for both directions after hello. Exceeding it is a framing error. Bound
JSON nesting and field lengths in the eventual normative specification.

Example request payload, exactly 14 UTF-8 bytes:

```json
{"op":"hello"}
```

Header for that payload with request ID 1:

```text
46 4d 47 4f 01 01 00 00 00 00 00 01 00 00 00 0e
```

The short example uses default limits; a normal hello should include
`max_payload` and the host SDK version. A successful response echoes the request
ID and uses the envelope `{"ok":true,"result":{...}}`. An error uses
`{"ok":false,"error":{"code":"unsupported","message":"..."}}`.
Machine behavior follows `code`, not the human message.

Hello result must contain `protocol_major`, `firmware_version`, stable `device_id`,
per-boot `boot_id`, `board_id`, `max_payload`, `auth_mode`, and arrays of channels
and programming targets. Each channel describes ID, backend, controls, limits,
and current ownership. Each target describes target ID, board/MCU identification,
programming mechanism, allowed formats, maximum image bytes, and recovery support.
Discovery metadata never replaces this response. Unknown optional result fields
may be ignored; incompatible major versions must be rejected.

Draft error codes: `unsupported`, `invalid_argument`, `busy`, `unauthorized`,
`wrong_target`, `too_large`, `integrity_failed`, `invalid_state`, `timeout`,
`io_error`, `response_too_large`. Errors must not silently perform the rejected
operation. The development contract identifies the implemented subset.

## Draft serial methods

Requests below are JSON objects in type-1 frames; responses are type 2.
Only hello and any eventually specified authorization exchange are allowed before
session authorization. One connection owns a given open channel at a time.

| `op` | Request fields | Result/behavior |
| --- | --- | --- |
| `hello` | Optional `max_payload`, `sdk_version` | Identity, limits, capabilities and authorization requirements |
| `serial.open` | `channel_id`, optional `config` | Acquire ownership; validate configuration atomically or reject |
| `serial.configure` | `channel_id`, `config` | Apply supported UART settings; reject unsafe changes while output is pending |
| `serial.control` | `channel_id`, `control`, `value` | Supported DTR/RTS/break or equivalent operation with explicit semantics |
| `serial.status` | `channel_id` | Queue usage, transmitted/received/lost counts, errors |
| `serial.close` | `channel_id` | Release ownership; discard pending queues under documented close policy |
| `device.reset` | Explicit target/scope | Authorized reset; acknowledgement is acceptance, not proof of reboot |

Type-3 serial payload: 4-byte channel ID followed by 1 or more unchanged data
bytes. Maximum data per frame is negotiated payload limit minus 4. Sender must
own that channel. Received bytes need not preserve frame boundaries. NUL, CR/LF,
and bytes above `0x7f` are ordinary payload. For channel 1 carrying `00 0d 0a ff`,
the payload is `00 00 00 01 00 0d 0a ff`, length 8.

An application stream has no meaningful baud setting. UART configuration uses
explicit baud/data-bits/parity/stop-bits/flow-control fields, constrained by the
advertised channel. Unsupported controls fail; never simulate a physical action.
Runtime console support requires an actual integrated runtime.

Use bounded queues and TCP backpressure. A host write completion only establishes
local transport progress, not target execution or even UART transmission. UART
overruns must be exposed via status/events. Interleave small frames fairly;
programming may require exclusive suspension of serial on the same target.
Disconnect releases channel ownership and discards unsent session buffers. Do
not automatically replay bytes after reconnect.

## Draft programming methods

The host provides a compiled image. Neither Firmingo nor this wire contract is
a compilation service. Programming the target and self-updating the bridge are
distinct target IDs and authorization decisions.

| `op` | Required inputs | Result/meaning |
| --- | --- | --- |
| `flash.begin` | `target_id`, `format`, `image_size`, lowercase hex SHA-256 `sha256` | Validate target/layout/resources; return `upload_id`, `max_chunk`, `next_offset=0` |
| `flash.status` | `upload_id` | State, confirmed next offset, last error and outcome evidence |
| `flash.finish` | `upload_id` | Validate complete staged image and hash; transition to `verified` |
| `flash.commit` | `upload_id` | Explicitly authorize installation of verified image; return accepted state |
| `flash.abort` | `upload_id` | Cancel staging where safe; reject during irreversible commit |

Type-4 chunk payload is `upload_id` (4 bytes), `offset` (4 bytes), then raw image
bytes. Give each chunk a request ID; wait for its type-2 acknowledgement before
sending the next chunk initially. Chunk size is bounded by both `max_chunk` and
negotiated payload minus 8. Offsets count image bytes, not frame bytes. This draft
limits images to the target-advertised size and at most `2^32 - 1` bytes.

Initial policy: sequential chunks only; acknowledge `next_offset` only after the
staging backend accepts those bytes. Do not claim power-loss durability unless
advertised by that backend. On a lost acknowledgement, query status; continue
from the confirmed offset. Reject out-of-order/overlapping chunks with the expected
offset. Resume across connection loss or reboot is unsupported unless advertised;
otherwise start a new upload after checking state. SHA-256 checks integrity, not
authorization or image provenance.

State model: `receiving → verified → committing → succeeded` or `failed`;
`receiving`/`verified` may become `aborted`. A successful `flash.finish` verifies
staging, not installation. A successful commit response means accepted. A network
disconnect is neither success nor proof of failure. Preserve enough operation
state to query an uncertain commit, or explicitly report `outcome_unknown` in
status until the target is inspected. Never blindly replay a commit.

For self-update, reconnect using stable identity and compare boot/image evidence.
For a bridged target, use programmer verification/readback where supported. If
new target software lacks a runtime identity service, report image verification
separately from confirmed application boot. Uploading arbitrary firmware may
remove Firmingo completely; document how the user recovers and reconnects.

Each target must specify accepted image format and address mapping, partition
limits, protected regions, signature policy if any, interruption behavior, and
bootloader/SWD recovery. Raw BIN, UF2, HEX, and ELF are not interchangeable.
Advertise only formats implemented and tested. For Arduino-Pico self-update,
account for its LittleFS staging requirement; staged OTA does not automatically
provide rollback. See the [Arduino-Pico OTA documentation](https://arduino-pico.readthedocs.io/en/latest/ota.html).

## Sessions, security, and SDK behavior

Request IDs are unique among outstanding requests within a connection. Responses
may arrive interleaved with serial frames and events. Draft events have
`{"event":"name","data":{...}}`; they are advisory and can be missed across
reconnect, so query authoritative status. Firmware must bound outstanding work.
The initial host implementation should issue one control request at a time.

Use bounded per-operation timeouts, with separate budgets for handshake, ordinary
control, and board-specific erase/program operations. Timeout leaves state
uncertain; it does not cancel a target action. Close/cancel explicitly where safe.
Automatic read-only reconnect/status checks are different from replaying writes.
A host suspended in the background may lose its session; reconcile on return.

Security is a **release-blocking design decision**, not solved by these examples.
The draft permits an explicitly labeled isolated development mode with no
application authentication. It must advertise that mode. Production profiles
must specify peer authentication, authorization, credential provisioning, and
transport protection before enabling destructive commands. Do not send secrets
over an unprotected channel or describe physical cabling as authentication.
A protocol claiming an unknown auth mode must be rejected by FirmingoKit.

FirmingoKit should expose discovery, device identity/capabilities, byte read/write,
channel controls, and programming progress/outcomes through a stable Swift API.
No Swift symbol names or package installation instructions are promised yet.
Apple host integration must handle local-network privacy, Bonjour declarations,
and applicable sandbox/transport-security configuration without blanket security
disablement. Declare the selected Bonjour type and explain local network usage
as appropriate for the deployment target; consult [Apple TN3179](https://developer.apple.com/documentation/technotes/tn3179-understanding-local-network-privacy).
Host permission denial is distinct from an absent device or protocol mismatch.

## Contract completion and interoperability gate

Before declaring protocol 1 implementable/stable, resolve the remaining draft
choices in `docs/protocol.md`: exact JSON schemas/enums and limits, target/boot
identity rules, authentication profiles, control electrical/timing semantics,
queue-close policy, operation timeouts, upload-state persistence and commit
idempotency, service registration and protocol evolution. Maintain one source of
truth; this README must link to the selected specification version.

Publish fixtures for hello, errors, binary serial frames, upload chunks, progress,
unsupported capabilities, malformed inputs, and reconnect/uncertain-commit cases.
The firmware suite and FirmingoKit Swift suite should consume the same byte
vectors. Record both revisions for end-to-end checks on each tested Apple OS.

Required checks include exact binary echo and UART behavior, fragmented/coalesced
TCP input, slow readers, wrong targets, exclusive ownership, rejected updates,
interrupted staging/commit, recovery, automatic attachment, Internet coexistence,
and connection timing. Do not count desktop compilation or Safari byte counters
as firmware-flashing or Swift interoperability evidence.

Application firmware now includes an experimental `device.diagnostics` control
method for sampled C heap, approximate core-0 stack and persistent Stream queue
peaks. `soak --diagnostics` requires and records these measurements. See the
[measurement limits](../../docs/protocol.md#sampled-runtime-diagnostics-experimental-additive-method);
the Nano now passes smoke and a two-minute measured soak on macOS.
[Recorded results](../../docs/evidence/application-diagnostics.md) preserve the measurement limits.

The hardware-tested application reference (`application-dev-v7`) replaces the automatic
Loopback backend with a reusable bounded `ApplicationEndpoint`. Arduino code now
explicitly reads and writes opaque host bytes, while lifecycle cleanup prevents
cross-connection replay. Native/sanitizer, compile and hardware evidence is
recorded in [the application-backend report](../../docs/evidence/application-backend.md).
V2 smoke and a two-minute measured soak pass, but four unassisted concurrent
Internet/64 KiB attempts stalled. A scoped TCP capture and the pinned core source
implicate an NCM receive worker that can strand queued input after its ten-packet
budget. V3 directly set the pending bit, but its first isolated 64 KiB hardware
replay failed with the same 6,656-byte return. V4 uses the SDK async-context wake
API after releasing the USB mutex and adds persistent NCM/raw-TCP diagnostics. It
passes smoke, one isolated replay, one Internet-overlap replay and a two-minute
soak. A refined two-connection burst also returned exact owner bytes and 64
recoverable control responses, but its budget/wake counters stayed zero. Hardware
has therefore not directly exercised the continuation branch, and these runs do
not yet establish production coexistence.
V5 adds a persistent maximum-frames-per-NCM-worker-run measurement so the next
hardware run can show how close normal traffic gets to the ten-frame budget.
V6 adds a persistent USB-mutex-contention counter for the earlier worker return
path; it does not change retry behavior. V7 adds the generic backend/configuration
boundary without changing the application channel behavior. It passes full
hardware smoke and a bounded diagnostic traffic run with concurrent Internet
access, and is now flashed on the development Nano. The separate UART profile
uses the same FMGO/NCM transport
with a bounded nonblocking `Serial1` adapter, explicit configuration, TX-drain
ownership gating, and RX overrun/loss diagnostics. V1 passed initial bounded
loopback and coexistence checks, then timed out after 393,216 exact soak bytes;
post-failure diagnostics proved UART RX overrun. `uart-dev-v2` reserves RX space
and backpressures local TX before that queue fills, and reports throttle events.
It passes the same unretried soak with 458,752 exact loopback bytes, seven
sessions, exercised throttling, zero overrun/loss, and concurrent Wi-Fi Internet
access. An independent Pico peer passes exact traffic in each direction at 115200
and 57600, and a controlled unsolicited burst verifies bounded overrun reporting,
drain, and exact recovery. A second peer image passes all 24 advertised
data/parity/stop-bit combinations at 57600, plus bounded exact checks at 300 and
2,000,000 baud. An uninterrupted 1,024-byte peer burst at 2 Mbaud overran the
Nano's 256-byte RX FIFO, confirming that the no-CTS profile cannot guarantee
arbitrary remote burst length. iOS remains open.
