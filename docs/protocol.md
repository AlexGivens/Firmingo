# Firmingo protocol 1 — development contract

Status: **development contract, not frozen**. Framing, JSON validation,
application/UART sessions, and bounded raw-lwIP TCP integration are implemented
and native-tested. Current firmware identifies as Firmingo 0.1.0 beta; exact
0.1.0 images require new hardware checks. Historical Nano/Pico development-image
results are indexed in [evidence](evidence/README.md). FMGO on iOS remains untested.
The opt-in Nano `m9exp1` resident adds the experimental managed-upload extension
below. Its exact Nano build has [successive upload, console, disconnect/status,
and pre-commit power-loss evidence](evidence/managed-sketch-m9exp1-hardware.md);
flash-write power loss and iOS remain untested. Release references continue to
advertise no upload targets. The [SDK handoff](integration/managed-sketch-sdk.md)
provides an exportable contract/vector bundle and reference build recipe.
The contract follows the historical
[product notes](research/roadmap-notes.md) for FirmingoKit.
The [application reference](../examples/firmingo_application/README.md) and
[UART reference](../examples/firmingo_uart/README.md) listen on provisional TCP 7420.
The separate
qualification profile provides HTTP/source/legacy echo. No legacy endpoint changed;
Bonjour advertisement is not implemented.

## Frame format

TCP read boundaries carry no meaning. A read can contain part of a frame or
several frames. Every frame has this 16-byte header followed by exactly the
declared payload length. All multibyte integers are unsigned big-endian.

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | Magic `46 4d 47 4f` (ASCII FMGO) |
| 4 | 1 | Protocol major, currently 1 |
| 5 | 1 | Type: 1 request JSON, 2 response JSON, 3 serial data, 4 upload chunk, 5 event JSON |
| 6 | 2 | Flags, must be zero |
| 8 | 4 | Request ID |
| 12 | 4 | Payload byte length, excluding header |

Request/response/upload types require nonzero IDs; serial/events require zero.
Unknown types, wrong IDs, nonzero flags, incorrect magic, unsupported major or
excessive length are rejected after the header, before payload collection. No
magic scanning or resynchronization occurs. Session returns a terminal protocol
error and the port must close. Client-sent response/event frames also close.
Profiles without an upload backend return `unsupported` for upload frames after
hello (`invalid_state` before hello). The experimental extension below handles
type 4 only when its target is advertised. Event emission is not implemented.

`core/protocol.*` owns one fixed 4112-byte frame buffer and allocates nothing.
`feed()` consumes at most `needed()`: remaining header, then remaining payload.
Callers retain coalesced tails. Fields/payload are read only in READY state and
remain stable until reset. READY/error states consume zero further bytes.
`reset()` discards the frame but retains the limit. Encoding checks capacity and
fields before writing; input/output must not overlap. The framing codec does not
validate JSON or authorize actions; `core/json.*` and `session.*` do that separately.

Before hello the receive limit is 4096. Device offer is 4096. Peer offers must be
integer values 512–65536; negotiated limit is the smaller offer and applies in
both directions. Session changes the decoder limit between frames after accepting
hello. A new connection starts with a new decoder/default limit. Different board
caps require explicit implementation/build evidence. This device emits serial
frames with at most 256 data bytes even when it accepts larger frames.

## JSON validation and schemas

Request payloads are UTF-8 JSON objects without BOM, trailing NUL or trailing
content. The validator enforces at most 96 tokens, eight nested containers,
and 64 decoded UTF-8 bytes per string. Root is depth zero; scalar values within
eight nested containers are accepted. Unicode escapes and valid surrogate pairs
are decoded; invalid/overlong UTF-8, isolated surrogates, raw control characters,
invalid escapes and embedded decoded NUL are rejected. Object keys must be unique
after decoding, including in nested objects. Standard JSON numbers are validated,
but fields requiring unsigned integers reject signs, decimals, exponents and
values above uint32. Unknown method names must fit 32 decoded bytes.

Syntactically invalid JSON, duplicate keys and validator resource-limit failures
release ownership and attempt one correlated `invalid_argument` response before
closing. Syntactically valid but wrong schemas receive `invalid_argument` without
side effects and can be corrected on the same connection. Unknown fields on
known requests are invalid. Unknown methods with a valid nonempty `op` string
receive `unsupported` after hello. Before hello, valid non-hello requests receive
`invalid_state`. No rejected control performs hardware actions.

Success is `{"ok":true,"result":{...}}`; failure is
`{"ok":false,"error":{"code":"...","message":"..."}}`.
Type-2 responses echo the request ID. Initial error message equals the machine
code; clients follow `code` and may ignore message/result fields they do not know.
Clients assign unique outstanding IDs. No replay/deduplication cache exists.
One outstanding control request is recommended; coalesced requests are processed
in order with one pending response. Backend serial frames can precede replies.

| Method | Accepted fields beyond required `op` | Result |
| --- | --- | --- |
| `hello` | Optional `max_payload` (u32, 512–65536), `sdk_version` (string, at most 64 decoded bytes) | Description below; no channel acquisition |
| `serial.open` | Required `channel_id` (nonzero u32), optional `config` object | Application accepts absent/empty config; UART accepts absent/empty config or a complete supported configuration |
| `serial.status` | Required `channel_id` | Owner's queue/counter snapshot |
| `serial.close` | Required `channel_id` | Release ownership, dispose session queues |
| `serial.configure` | Required `channel_id`, `config` object | UART owner applies a complete supported configuration when queues/TX are idle; application returns unsupported |
| `serial.control` | Required `channel_id`, nonempty `control` string (at most 32 decoded bytes), `value` | `break`, `dtr`, `rts` require boolean values; other selectors accept boolean/string/u32; all return unsupported |
| `device.reset` | Required `scope:"device"` | Unsupported; never acknowledges a successful reset |

Only channel 1 exists. A different channel returns `wrong_target`. Open by a
second connection returns `busy` without displacing/discarding the owner.
Repeat open by the current owner returns `invalid_state`; status/close/configure/
control require ownership and otherwise return `invalid_state`. Device reset is
unsupported regardless of channel ownership. An offline backend prevents open
with `io_error`. Hello is accepted once; repeats return `invalid_state`.

UART configuration requires `baud`, `data_bits`, `parity`, and `stop_bits`
together. Optional `flow_control` must equal `"none"`; other or unknown fields
are invalid. The current Nano port accepts baud 300–2,000,000, data bits 5–8,
parity `"none"`, `"even"`, or `"odd"`, and one or two stop bits. Open with no
effective configuration retains the current settings. `serial.configure` reports
the requested `baud`, hardware `actual_baud`, the other effective fields, and
`flow_control:"none"`. It returns `busy` while either portable Stream queue or
the UART transmitter is nonempty. A successful change drains and counts unread
UART RX bytes before reinitializing hardware; the external sender must pause
during reconfiguration because this profile has no CTS.
For 5-, 6-, and 7-bit UART formats, only the low configured number of bits from
each network byte can appear on the wire; callers must constrain values to that
range when exact byte equality is required. The 300–2,000,000 range describes
accepted hardware configuration, not unlimited sustainable input. With no CTS,
an uninterrupted remote burst larger than available RX buffering can overrun at
any baud; a 1,024-byte 2 Mbaud burst has produced this condition on the Nano.

## Identity and capabilities

Hello result contains `protocol_major`, `firmware_version`, `device_id`, `boot_id`,
`board_id`, `max_payload`, `auth_mode`, `channels` and `targets`. Device/boot IDs
are 16 lowercase hex characters. Board/firmware names are nonempty ASCII names
of at most 32 bytes using letters, digits, underscore, period or hyphen. The port
supplies these values; invalid local identity prevents a Session from running.

The Nano port must render unique-board-ID bytes as lowercase hex, matching its
existing USB/diagnostic ID (example `a1b2c3d4e5f60718`); IP is not identity.
Boot ID must distinguish boots and is shared across sessions for one boot.
Generation is a board-port responsibility and is not implemented by the portable
component. Native fixtures supply explicit test identities; they are not live
boot evidence. A host verifies expected device/backend before serial open/data.
Identity is target selection, not cryptographic peer authentication.

Hello reports `auth_mode:"open-development"`, one channel with ID 1, empty
controls, Stream rx/tx queue capacities of 256 bytes, and a snapshot boolean
`owned`. This describes channel availability, not ownership granted by hello.
`targets` is empty in the release references. Application v7 reports `backend:"application"` and offers no
configuration. UART v2 reports `backend:"uart"`, the current configuration,
`configurable:["baud","data_bits","parity","stop_bits"]`, and its baud limits.
Neither profile offers a runtime, files, reset, interruption, hardware/software
flow control, or programming. Application v7 and UART v2 have bounded macOS
hardware evidence; UART v1 retains its preserved soak failure. Production
authentication and authorization need separate design before destructive capabilities.
The experimental target's physical authorization policy is described below;
`auth_mode` still describes the unauthenticated development session.

## Byte stream, counters and acknowledgements

Type-3 payload is a four-byte channel ID followed by one or more opaque bytes.
It requires a successful hello/open. Maximum incoming data is negotiated payload
minus four. Empty data, missing/wrong channel prefix or data without ownership
closes with protocol error, without injecting an error into serial bytes. Clients
wait for successful open before sending data.

NUL, CR/LF, escape values and bytes above 0x7f are ordinary data, preserved in
order per direction. Application message boundaries need not match frame
boundaries. Logs/control errors never enter byte data. Socket adapters are
nonblocking: partial writes retain tails, bounded queues apply backpressure,
and one stalled direction does not stop the other until its own queues fill.
Command replies and backend output alternate when both remain ready, avoiding
indefinite starvation. Each poll makes at most one transport read/write and
one backend read/write. Session stores one input frame and a 1040-byte framed output
buffer; Channel stores the Stream's two 256-byte queues. The Nano application
backend stores two additional 256-byte circular queues. Other backend/stack
queues are separate explicit bounds.

Open success means ownership acquired. Close success means unsent session/Stream/
backend queues disposed and ownership released. Bytes already copied into the
transport cannot be recalled. Status success means a snapshot was taken; it is
not a flush/completion barrier. Counter fields are `backend_rx_bytes` (accepted
by backend write), `backend_tx_bytes` (extracted by backend read),
`pending_to_backend`, `pending_to_peer` (Stream queues only, excluding Session
frame buffers and transport/backend queues). Counters are u64, reset on acquire,
and saturate at UINT64_MAX. Tx extraction is not delivery/application completion.

Serial data has no general application-completion acknowledgement. TCP/write
completion does not prove that an application command executed. Applications
requiring completion must define correlated acknowledgements within their byte
protocol. Unsupported reset acknowledges rejection only and invokes no reset.

Implemented error codes are `unsupported`, `invalid_argument`, `busy`,
`wrong_target`, `invalid_state`, `io_error`, and `response_too_large`. The last
means a valid dynamic reply exceeds the peer's negotiated payload; the compact
error reply is returned and the connection remains usable. Other README codes
are reserved for future services. Malformed framing/invalid direction closes immediately, with
no required JSON response. Invalid JSON attempts a response for at most 1000 ms;
the absolute flush deadline is not extended by progress, and delivery is best
effort. The port closes on every terminal Session result.

## Timeouts, disposal and adapter responsibilities

Hello must be accepted within 5000 ms of Session creation. Incomplete frames have
a 5000 ms assembly deadline from their first received byte. Those are absolute
and are not extended by trickles or invalid requests. After hello, 30000 ms without
actual transport/Stream progress expires the session. A completed frame held
behind backpressure remains bounded and subject to idle expiry. Timers expire
before I/O at the exact boundary; tests include uint32 clock rollover.

Disconnect, close, timeout, destruction and I/O failure discard input/output and
Stream queues, release ownership and call `StreamBackend::discard()` for the
owner's backend queues. UART bytes already accepted by hardware TX cannot be
recalled, so a new owner receives `busy` until hardware transmission drains.
Unread UART RX is discarded and counted before acquisition and on owner cleanup.
Unowned sessions do not discard another owner's data.
Discard must not reset unrelated application state. Backend failure also releases
the channel. The port separately disposes socket/pbuf queues, treats half-close
as full disconnect, and supplies unique nonzero tokens per concurrent connection.
The backend outlives its Channel, which outlives all Sessions. All calls are
serialized in application context under the board's required stack lock, never
concurrent USB callbacks. Counts must respect supplied capacities.

Reconnect performs a fresh hello/open. No pending command/response is inherited
or automatically replayed. A disconnect proves neither completion nor failure
of previously transmitted application commands. Clients must not automatically
replay uncertain writes. The application and UART reference sketches instantiate
the same portable Session/Channel component; their networking/startup behavior
still needs profile-specific hardware evidence.

## Exact shared fixtures and compatibility

Hello request 1, exactly 14 JSON payload bytes `{"op":"hello"}`:

```text
46 4d 47 4f 01 01 00 00 00 00 00 01 00 00 00 0e
7b 22 6f 70 22 3a 22 68 65 6c 6c 6f 22 7d
```

Channel 1, data `00 0d 0a ff`:

```text
46 4d 47 4f 01 03 00 00 00 00 00 00 00 00 00 08
00 00 00 01 00 0d 0a ff
```

`tests/fixtures/protocol-v1/` contains complete hex frames for those inputs,
the canonical hello response, reset request 3 and its unsupported response.
Hello fixture identity uses boot ID `0123456789abcdef`, board
`nano_rp2040_connect`, firmware `session-dev-v1`, limit 4096 and unowned channel.
Native tests compare actual production output byte-for-byte against fixtures.
The reset response header is:

```text
46 4d 47 4f 01 02 00 00 00 00 00 03 00 00 00 43
```

Its 67-byte payload is exactly
`{"ok":false,"error":{"code":"unsupported","message":"unsupported"}}`.
This increment retains the established FMGO framing and adds experimental
request/result schemas. Intentional future wire changes require updated vectors
and compatibility notes. Legacy qualification ports still accept their original
formats, and no Swift/board interoperability is certified by native tests.

Next: extend physical coverage beyond 8N1 at 115200/57600 or return to attachment
and iOS qualification. Additional UART controls and updates remain separate work.

## Nano TCP port and reference application

`ports/arduino_pico/tcp_session.*` owns two fixed Session slots and one retained
pbuf chain per socket, with additional packets retained by lwIP's refused-data
path. Receive credit advances only when bytes reach the decoder; input stops
when the fixed frame/stream/backend/output queues fill. `tcp_write` copies at
most available send-buffer space; ERR_MEM retains the unsent tail. There is one
session poll/output attempt per socket per application turn. No parser/backend
work runs in USB/TCP callbacks. All port/backend calls require the Ethernet lwIP
lock; no API here is thread-safe without that serialization.

Accepted connections start their hello deadline at acceptance, before first poll.
One application-loop pass disposes disconnected sessions and releases ownership
before processing other clients. EOF discards queues; TCP half-close is unsupported.
An error callback invalidates the already-freed PCB without accessing it later.
Only two sockets have storage; excess connections get RST rather than displacing
an owner. The second protocol session can negotiate and receive channel BUSY.

Terminal sessions release ownership immediately. The adapter allows at most one
additional second for TCP acknowledgments of copied output, then tries FIN close.
Temporary close-memory failures retry once per application turn within that same
deadline; expiration aborts. Success transfers PCB lifetime to lwIP and detaches
all callbacks, so slots cannot be referenced after reuse. The pinned lwIP can
send RST for refused/unconsumed input; all copied output is acknowledged before
a close attempt. Fatal error delivery remains best effort; TCP acknowledgment
is not evidence that an application consumed a result. This adapter deadline is
additional to Session's one-second fatal-JSON output-queue flush deadline.

`StreamBackend` receives `opened(owner)` after old data is discarded and a
successful channel acquire. A backend can reject acquisition with `busy` before
ownership changes; UART uses this while its transmitter is active. Release,
disconnect, timeout and I/O failure invoke
`closed()` and then `discard()` in application context. Busy/invalid-owner attempts
do not notify or alter the backend. Callback code still only records transport
state; it never runs these lifecycle methods.

The reference application uses `ApplicationEndpoint` on channel 1 and a stable
lowercase Pico board ID. The endpoint exposes nonblocking application-side
`read_from_host` and `write_to_host` calls backed by two 256-byte circular queues.
Queue saturation returns a partial count, applying backpressure without loss.
Calls while inactive transfer no bytes. Close/disconnect clears pending input and
output before another owner can acquire the channel; boot-lifetime peak and
discard counters persist. The sketch's explicit application code echoes bytes to
retain the hardware integrity test, but echo is no longer backend behavior.
`get_rand_64()` supplies one boot nonce, formatted
as 16 lowercase hex digits and reused across sessions in that boot. It is neither
an authenticator nor a guaranteed unique persistent counter. DHCP is local-only,
NCM-only USB attaches after all services are ready, and NINA Wi-Fi is unused.
This application profile advertises no UART, runtime, reset, or update capability. See
[integration evidence](evidence/tcp-session.md); native raw-stack fakes do not
establish real lwIP/USB/host behavior.

The separate Nano UART reference uses `Serial1`/RP2040 `uart0`, D1/TX GPIO 0,
and D0/RX GPIO 1. Its adapter fixes the Arduino-Pico receive FIFO at 256 bytes,
reads only currently available bytes, writes only while hardware reports space,
and checks the pinned UART busy register without waiting. UART v2 keeps 64 RX
bytes in reserve before accepting more locally originated TX; this backpressures
an echoing/responding target when network output stalls. It does not stop an
independent remote transmitter. No UART call runs in a
USB callback. See [UART evidence](evidence/uart.md); compile and native results do
not establish pin, baud, overrun, or coexistence behavior on hardware.

The Nano raw port dispatches `sys_check_timeouts()` at the beginning of each
application poll under the lwIP lock. The pinned SDK's background timeout worker
can be repeatedly postponed by frequent lock releases, so a raw-only loop cannot
rely on it for refused-data retry, retransmission and delayed ACK timers. Timer
callbacks record transport changes; disconnected/replacement session cleanup
follows before other sessions can acquire the channel. A new accept can reserve
an already-dead PCB's slot before that cleanup, without a third live PCB or
executing backend work in the callback. Input for that replacement is refused
until cleanup and retried by lwIP. Listen allocation exhaustion may report a null
PCB; the callback returns its error without attempting to abort a null pointer.

The hardware harness observes peer EOF/RST before reporting TCP closure and
opening a fresh session. Closing only the local descriptor does not establish
remote closure. This bounded wait has no reconnection retry; timeout/unread data
fails the test. The previous smoke failures remain recorded in
[hardware evidence](evidence/application-hardware.md). Timer starvation is a
source-backed explanation supported by passing smoke/64 KiB tests after timer
servicing was added; the precise packet-level sequence remains untraced.


## Sampled runtime diagnostics (experimental additive method)

After hello, `{"op":"device.diagnostics"}` requires no channel ownership and
accepts no extra fields. It returns `unsupported` when this port supplies no
samples. Existing hello, serial/status and FMGO framing are unchanged; older
firmware returns `unsupported` for this method. Complete byte vectors are
`tests/fixtures/protocol-v1/diagnostics-{request,response}.hex` (request ID 9).

A result contains `samples`, `heap_free`, `heap_min`, `stack_free`, `stack_min`,
`lwip_free`, `peak_to_backend`, and `peak_to_peer`. Backends may add an atomic set
of `application_rx_pending`, `application_tx_pending`, `application_rx_peak`,
`application_tx_peak`, `application_rx_discarded` and
`application_tx_discarded`. The current Nano application endpoint supplies all
six; other backends omit the whole set. Sizes are bytes; counters are
unsigned 32-bit numbers. `samples` saturates at UINT32_MAX. In the Nano reference,
the application firmware also adds one atomic transport group:
`ncm_worker_runs`, `ncm_rx_frames`, `ncm_rx_deferred`, `ncm_rx_batch_peak`,
`ncm_mutex_contentions`, `ncm_budget_exhaustions`, `ncm_wake_requests`, `tcp_accepts`,
`tcp_rx_callbacks`, `tcp_sent_callbacks`, `tcp_errors`, `tcp_rx_bytes`, and
`tcp_sent_bytes`. The first eleven are saturating unsigned 32-bit counters; byte
counters are saturating unsigned 64-bit counters. Consumers must accept the whole
group or reject a partial group. `ncm_rx_batch_peak` is bounded by the production
worker limit of 10 and persists for the boot.

The experimental managed-sketch application backend also supplies the optional
fields `sketch_input_discarded`, `sketch_output_discarded`,
`sketch_output_rejected`, `sketch_output_peak`,
`sketch_loop_boundaries`, `sketch_epoch`, `sketch_acknowledged_epoch`,
`sketch_input_enabled`, and `sketch_output_enabled`. The first two count bytes
already accepted by a bridge queue but disposed during an owner or sketch
generation change. `sketch_output_rejected` counts bytes refused by a short
or pre-open `Serial.write`; a caller that retries may avoid data loss. The peak
is the bridge's core-1-to-core-0 queue occupancy, bounded by 256. The loop
boundary count shows core-1 progress. The epoch fields and booleans show when
a new owner is waiting for the next sketch loop boundary. The counters are
boot-lifetime saturating u32 values, separate from the application endpoint's
own queue counters. The optional group is emitted when the host negotiates a
payload limit of at least 1024 bytes; smaller offers omit the whole group.
The fields are individually sampled and do not form a transactional snapshot.
Other application backends omit the whole group.

In the Nano reference, heap fields use Arduino-Pico's C allocator accounting;
stack fields approximate available core-0 stack. The loop samples immediately
before and after server
work. Minima persist for the boot, but miss transient allocations and deeper
parser/callback/interrupt stack usage. Fragmentation can prevent an allocation
smaller than the reported free heap. The pinned lwIP uses a separate fixed memory
pool with statistics disabled: `lwip_free:null` explicitly marks it unmeasured.

Queue peaks measure each portable Stream's occupied bytes immediately after a
read, before the corresponding write, including bytes drained in the same poll.
They are bounded by 256 and persist across channel close/disconnect/reacquisition.
The Stream peaks exclude backend queues, Session framing/output and lwIP buffers.
Application pending values are instantaneous; its peaks and u64 discarded-byte
counters persist across close/reacquisition for the boot. A discard count records
lifecycle cleanup, not queue saturation or successful consumption. UART profiles
instead add the atomic set `uart_rx_pending`, `uart_rx_discarded`,
`uart_rx_overrun_events`, `uart_rx_lost_bytes_minimum`, and
`uart_tx_throttles`. A hardware overrun
increments both the event count and the conservative minimum lost-byte count by
at least one; the actual loss can be larger. `uart_tx_throttles` counts local
write attempts deferred to retain the RX reserve. All persist for the boot.

The maximum UART-plus-transport diagnostic is 776 payload bytes. A peer that
negotiates a smaller payload, including the permitted minimum 512, receives
`response_too_large` for that request. Diagnostics are control responses; no
diagnostic bytes enter the stream.
This endpoint is observational and does not establish memory stability alone.

## Experimental Nano managed-upload extension (M9)

This extension exists only in `m9exp1` with an explicit `UploadService` backend.
It is not present in the 0.1.0 reference UF2s. The exact resident ELF and modules
must be built together with Arduino-Pico 6.0.0. No arbitrary Arduino library,
mutable-global, whole-board UF2, filesystem, or rollback support is implied.

Its hello advertises this one target; hello's `board_id` identifies the board:

```json
{"id":1,"format":"nano-managed-v1","max_size":4096,"max_chunk":1024,"authorization":"physical-d2-gnd"}
```

The accepted capsule is the [Nano FMS1 format](research/managed-sketch-contract.md):
64-byte little-endian header, zero padding to offset 256, and code at
`0x10200100`. The header must match the Nano board tag, ABI, exact resident API
address, code size, Thumb entry points, and code SHA-256. `flash.begin` supplies
another SHA-256 over the **whole capsule**, including header and padding. The
RAM stage holds 257–4096 bytes; only the first 4096-byte slot sector at
`0x10200000..0x10201000` can be erased/programmed. The reserved resident,
staging, metadata, and EEPROM ranges are not network-supplied destinations.
The build tools enforce the same 4096-byte capsule limit.

The FMS1 header has these fixed offsets; multibyte header fields are
**little-endian**, unlike FMGO frame and chunk fields. The whole capsule has
exactly `256 + code_bytes` bytes; there is no transport header in a `.fms` file.

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | Magic `FMS1` |
| 4 | 2 | ABI version, 1 |
| 6 | 2 | Header bytes, 64 |
| 8 | 4 | Nano tag, `0x4f4e414e` |
| 12 | 4 | Exact resident API table XIP address, aligned to four bytes |
| 16 | 4 | Fixed code XIP address, `0x10200100` |
| 20 | 4 | Nonzero code byte count; whole capsule must fit the target limit |
| 24 | 4 | Thumb setup entry address (low bit set), within the code range |
| 28 | 4 | Thumb loop entry address (low bit set), within the code range |
| 32 | 32 | SHA-256 of code bytes only, as raw digest bytes |
| 64 | 192 | Required zero padding |
| 256 | `code_bytes` | Linked code/constant bytes |

The capsule's header/API binding is checked by the resident. The local build
report separately identifies the exact resident ELF, whole capsule, code and
source hashes; these are not additional hello fields or remote attestation.

### Requests and acknowledgements

All fields shown are required; unknown fields are invalid. Control requests
use type 1 with a nonzero request ID, and receive correlated type-2 responses.
Numeric fields are nonzero u32 except chunk offset, which may be zero.

| Operation | Fields beyond `op` | Success means |
| --- | --- | --- |
| `flash.begin` | `target_id:1`, `board_id:"nano_rp2040_connect"`, `format:"nano-managed-v1"`, `size`, `sha256` (64 lowercase hex characters) | One RAM stage acquired; returns a new `upload_id` and `accepted` state |
| Type-4 chunk | Binary payload below; no JSON `op` | Exact chunk copied to RAM and `received` advanced; not flash completion |
| `flash.status` | `target_id:1`, `upload_id` | Retained transaction snapshot; allowed from a new connection |
| `flash.finish` | `target_id:1`, `upload_id` | Complete capsule and both digests/header validated in RAM; `verified` |
| `flash.commit` | `target_id:1`, `upload_id` | Install queued; `committing`. No flash write occurs in request dispatch |
| `flash.abort` | `target_id:1`, `upload_id` | An accepted/verified stage disposed; `aborted`. Cannot cancel commit |

Type-4 payload consists of a four-byte big-endian `upload_id`, four-byte
big-endian **capsule-relative** offset, then 1–1024 opaque image bytes. It is
also bounded by negotiated payload minus eight. It is not a flash address.
Offsets must equal the current `received` count. Empty, duplicate, out-of-order,
wrong-owner/ID, or overrun chunks are rejected without advancing the stage.
There is no chunk replay cache. A client waits for each acknowledgement; if a
chunk response is lost, it queries status to determine whether bytes were
accepted rather than blindly resending. Loss of that connection before commit
aborts the stage.

For upload ID 1, offset 0, and four test bytes `00 ff 0d 0a`, request ID 9:

```text
46 4d 47 4f 01 04 00 00 00 00 00 09 00 00 00 0c
00 00 00 01 00 00 00 00 00 ff 0d 0a
```

Every successful upload response uses this snapshot schema (synthetic digest):

```json
{"ok":true,"result":{"upload_id":1,"state":"verified","size":692,"received":692,"sha256":"0000000000000000000000000000000000000000000000000000000000000000","slot_touched":false,"error":"none"}}
```

### State, ownership, and authorization

| State | Meaning / next transition |
| --- | --- |
| `accepted` | RAM stage exists; chunks, finish, abort, or disconnect |
| `verified` | RAM validation passed; commit, abort, or disconnect |
| `committing` | Queued/awaiting core-1 park/install; eventually installed or failed |
| `installed` | Flash readback and image validation passed; new generation scheduled |
| `boot_confirmed` | New generation's setup and first loop returned to the resident |
| `aborted` | Pre-commit stage disposed; no slot write |
| `failed` | Validation/park/install failed; inspect `error` and `slot_touched` |

`boot_confirmed` here confirms a **managed sketch start**, not a whole-device
reboot, useful behavior, exact console echo, or future stability. A nonreturning
new setup/loop can leave `installed` without confirmation. The Nano port
reports confirmation from a core-1 generation acknowledgement. `slot_touched`
is conservative: true after an erase attempt, including a failure that could
leave a partial sector. The old sketch is stopped before that attempt and is
never resumed after an uncertain slot write.

Only one upload transaction may be active. Its originating session owns chunks,
finish, commit, and abort; another session may only read its status. Console
ownership and active staging/commit are mutually exclusive (`busy`); plain
hello/diagnostics/status connections may remain open. Once install completes,
console acquisition is permitted again. The newest transaction's snapshot is
retained in RAM until another begin replaces it or the board reboots. IDs are
nonzero u32, monotonically allocated per boot, never reused within that boot;
ID exhaustion refuses another begin. Hosts associate an upload ID with both
device ID and boot ID. There is no transaction history or persistent journal.

Session authentication remains `open-development`. Upload authorization requires
Nano **D2 / GPIO25 connected to GND** at begin, commit acceptance, and immediately
before the erase path. Removing the arm while waiting to park fails without
writing. Keeping the jumper installed permits any reachable development peer
who acquires ownership to submit arbitrary compatible code. SHA-256 provides
integrity, not authorization, signature verification, or provenance. This is an
owner-controlled proof policy, not production network security. The NINA module
remains unused and DHCP remains local-only.

Additional extension errors are `unauthorized`, `wrong_owner`, `wrong_offset`,
`incomplete`, `digest_mismatch`, `invalid_image`, and `insufficient_storage`.
Wrong target/board/format returns `wrong_target`. Incomplete finish permits more
chunks. Digest/image failure clears the stage and retains a `failed` snapshot.
Status `error` can additionally be `disconnected`, `park_timeout`, or
`install_failed`; the ordinary response still succeeds when reading a failed
transaction. Repeated commit, post-commit abort, or actions in a terminal state
return `invalid_state`; they never repeat flash work.

### Disconnect, reboot, and power interruption

Disconnect/idle timeout/malformed JSON before commit disposes RAM staging and
retains `aborted` with `error:"disconnected"`. Abort does not replace the old
sketch. A commit is queued exactly once; its execution continues after the
session closes, subject to physical arm and core-1 park checks. The port calls
installation from its main loop **outside** the Ethernet/lwIP lock and USB
callbacks. Core 1 must park in SRAM within 1000 ms; timeout preserves the slot.
Core 0 masks interrupts during erase/program. Each operation is bounded to one
sector and its pages, then read back before scheduling the new generation.

A lost commit reply is an uncertain outcome: a fresh hello must match the same
device/boot, then `flash.status` queries that upload ID and digest. The host must
not replay commit. If the boot changed or the retained record was superseded,
RAM status cannot establish the old transaction's result. Reboot discards staging
and status; startup independently validates the installed slot. Power loss can
leave the old, new, or partial image depending on when it occurs. There is no
rollback, atomic power-cut guarantee, or boot-confirmation journal. Preserve
Nano REC/GND ROM recovery. M9 has [pre-commit disconnect and discarded-result
status and verified-RAM-stage power-loss evidence](evidence/managed-sketch-m9exp1-hardware.md).
Power loss during erase/program remains untested. Power-cut cases are opt-in,
separate from smoke and CI.

Shared complete frames and a non-executable capsule fixture are in
[`tests/fixtures/protocol-v1/managed-upload`](../tests/fixtures/protocol-v1/managed-upload/README.md).
Native tests feed those vectors through the production Session, stage, and
switch. Host unit tests check sender/reconciliation behavior with fake peers;
they do not establish hardware success. See the
[M9 build record](evidence/managed-sketch-m9exp1.md) for actual commands/results.
The shared vectors also include M9 hello, accepted/chunk acknowledgements,
committing, installed, boot-confirmed, explicit abort, disconnect abort,
install-failed status, and an unauthorized response. Native tests compare their
complete bytes against the production Session/stage/switch. This adds test
coverage without changing wire version, operation schemas, or runtime behavior.
