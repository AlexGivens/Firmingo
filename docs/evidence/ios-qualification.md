# iPhone/iPad qualification evidence — 2026-09-23

## Current status

Hardware attempts started: **3**; completed: **3**. Attempt 1 established
authorization, automatic local-only addressing, omission of router and DNS
configuration, access to the expected device identity, and exact browser-stream
integrity. An external HTTPS page also loaded while the Nano remained attached,
after which the local validator remained healthy with an increased count. This
was followed by a successful previously authorized reconnect: iPadOS displayed
the same per-connection authorization prompt, and acceptance restored the exact
stream without RESET or manual network changes. A connection made while the iPad
was locked also became usable after unlock, with automatic Ethernet and an exact
stream. These are three completed physical connections, not yet the target 20
cold connections. The staged manual procedure is in
[iPhone/iPad qualification](../ios-qualification.md).

## Prepared qualification image

The current `local-only` profile was rebuilt with Arduino CLI 1.5.1 and
Arduino-Pico 6.0.0 using the pinned deferred NCM-only startup overlay, local-only
DHCP, and subnet `192.168.77.0/24`. It compiles to 119,680 flash bytes and 77,136
static RAM bytes. The 274,432-byte UF2 SHA-256 is
`4b35802d46102d86b7f6408d74126857d6696bd78dcc9613a2a2ac949cfe7965`.
The six warnings are the already recorded upstream `WiFiClient::write(uint8_t)`
hidden-overload warnings; no project-source warning was emitted.

Preserved [build report](ios-qualification-build.json) and
[firmware log](ios-qualification-firmware.log) establish compilation only.
All 153 existing host regression tests also pass; preserved
[JUnit report](ios-qualification-host.xml). These host cases do not add an iOS
hardware attempt.

## Mac preflight after flashing

The report-verified UF2 was copied through the explicitly selected Nano
`RPI-RP2` ROM volume. On its first automatic attachment to macOS, `en5` received
`192.168.77.16/24` without RESET or manual configuration. `/diagnostics` reported
firmware `ncm-startup-v1`, board `nano_rp2040_connect`, device ID
`a1b2c3d4e5f60718`, NCM-only USB, and `usb_initialized_at_setup:false`; DHCP,
NCM, services, and final attach request were recorded at 2/2/3/3 ms after setup.

The existing three-case hardware smoke passed once:

- 20 exact deterministic source records: 5,440 bytes.
- 65,536-byte fragmented full-duplex echo and 4,096-byte reconnect echo.
- 65,536-byte delayed-reader echo concurrent with another 5,440 exact source
  bytes.

The live HTTP path then returned the expected diagnostic page and 100 exact
records (27,200 bytes) through `/stream`, with magic, sequence, header, and every
payload byte checked. DHCP inspection showed server `192.168.77.1`, mask
`255.255.255.0`, and 600-second lease, with no router or DNS option. The board
route used `en5`; the Internet default remained Wi-Fi `en0` through
`192.168.4.1`. A fresh IPv4 HTTPS request returned status 200 from
`172.66.147.243`.

This preflight establishes the flashed image and Mac behavior only. It does not
add an iPhone/iPad attempt or prove Safari executes the embedded validator.

## Browser validator preparation

The diagnostic page now validates complete 272-byte deterministic records rather
than treating an increasing count as proof. It checks magic, continuous sequence,
length, reserved flags, and every payload byte while buffering arbitrary browser
stream splits/coalescing. The exact JavaScript embedded in the compiled sketch was
extracted locally: Node 22.12.0 accepted its syntax, parsed two records split at
13 and 400 bytes, and rejected a corrupted payload. This is board-free page-logic
evidence. It does not establish Safari behavior, live USB attachment, or exact
browser-to-board binary transfer.

## Attempt ledger

| Attempt | Device / OS | Initial state | Result | Evidence |
| ---: | --- | --- | --- | --- |
| 1 | iPad Pro 11-inch (3rd generation), MHQW3LL/A, iPadOS 27.0 | Unlocked cold connection | **Pass:** authorization, automatic local-only lease, identity, browser-stream integrity, and Internet coexistence | Direct USB-C to Micro-USB cable previously used for Mac preflight; screenshot observations and original hashes below |
| 2 | Same target | Previously authorized reconnect | **Pass:** per-connection authorization followed by immediate usable exact stream without RESET or manual IP changes | User observation; no screenshot or measured latency |
| 3 | Same target | Connect locked, then unlock | **Pass:** Ethernet appeared automatically and exact browser records became usable without RESET or manual IP changes | User observation; exact prompt/lock-screen behavior and latency not reported |

### Attempt 1 observations

With the iPad unlocked and connected to a private Wi-Fi network, attaching the unpowered
Nano produced the prompt “Allow accessory to connect? Do you want to connect the
USB accessory to this iPad?” immediately. The user selected **Allow**. Ethernet
then appeared in Settings with one interface named `Nano RP2040 Connect`.

The interface detail showed automatic IPv4 configuration, address
`192.168.77.16`, mask `255.255.255.0`, and an empty Router field. HTTP proxy was
off. The DNS detail showed Automatic selected with empty DNS Servers and Search
Domains sections. A message stated that DNS requests were being routed by iCloud
Private Relay for the Wi-Fi network; it did not list a DNS server for the Nano's
Ethernet interface. Wi-Fi remained shown as connected in Settings.

Safari reached `http://192.168.77.1/diagnostics` and received the expected JSON
identity: firmware `ncm-startup-v1`, board `nano_rp2040_connect`, device ID
`a1b2c3d4e5f60718`, USB profile `ncm-only`, and
`usb_initialized_at_setup:false`. It reported setup and DHCP ready at 1 ms, then
NCM, services, and attach request at 2 ms. These are firmware-relative startup
timestamps, not an observed DHCP lease latency.

The root diagnostic page ran in Safari for about 40 seconds and remained green
with `exact records validated`. It validated 815 consecutive records and 221,680
bytes, reported next sequence 815, and displayed a current rate of 5,418 bytes/s.
No mismatch was reported. This establishes exact payload integrity for that
bounded browser run; it is not a throughput limit or a long-duration stability
result.

With the Nano still attached, Safari successfully loaded the requested external
HTTPS Example Domain URL. Returning to the local test showed the validator still
green at 5,205 records and 1,415,760 bytes, with next sequence 5,205. This is an
increase of 4,390 records and 1,194,080 bytes from the earlier screenshot. Safari
may schedule a background tab, so the screenshots do not establish uninterrupted
transfer during every instant that the external tab was foregrounded; they do
show working Internet access with the Nano attached and an intact local stream
before and after the external request.

Exact attachment/lease latency was not measured; “immediately” is a user
observation. No RESET, reconnect, or manual IP change was reported. Attempt 1 is
therefore recorded as a pass for the bounded unlocked cold-connection procedure.

The seven original screenshots are withheld from the public repository because
some show the tester's account, private Wi-Fi network, or unique hardware ID,
and the PNG metadata includes capture timestamps. Their observations above are
transcribed with identifiers pseudonymized. Original SHA-256 values remain for
private verification:

| Screenshot | Original SHA-256 |
| --- | --- |
| Authorization prompt | `35a49336e0f1d938b1fd7ff4cffee1b0532ad730202bdafb464f17ef9a077d5d` |
| Network detail | `61094db83b2af285ad7cff0e14079d7a0f287fa92fc873ed7d555c34f6206213` |
| DNS detail | `75999780745e0a1ad04695976b986ba7ff7e9a77f4e9dc883fb042dac6c432c9` |
| Safari diagnostics | `5ad733c11e915d0368895ceca578196b5c36a1272111eb525fd827f1b5c479f2` |
| Browser stream | `6475a57e1bfba0566a7cc0405e675c11d5ad835245d1b500e373f54480ed7f04` |
| External Internet page | `906027b3e4dd0a962b4e0214f73bb84efcf64315383b0bac4acc7655e559d66e` |
| Local stream after Internet access | `0c2c0451893f1bd924d32cb1ff943d3352ff59e27e468e2da32fe659fc4a73fd` |

### Attempt 2 observations

The user disconnected the Nano, waited for Ethernet to disappear from Settings,
and reconnected it while the iPad was unlocked. The same “Allow accessory to
connect?” prompt appeared again. The user considers this per-connection host
authorization desirable for the iPad's security boundary and selected **Allow**.

After acceptance, reloading the local page immediately produced green `exact
records validated` status. Because that state is displayed only after Safari has
received and checked at least one complete deterministic record, the observation
establishes a fresh usable DHCP/IP/HTTP stream after reconnect. No RESET, repeated
cable reconnect, manual IP change, or other recovery was reported. Exact lease
and first-record latency and the final record count were not measured, and no
screenshot was captured. Internet coexistence was already exercised in attempt 1
and was not repeated in attempt 2.

The repeated prompt is recorded as expected host policy for this target rather
than a firmware failure. A future host SDK may detect reachability and reconnect
after the user approves the accessory, but it must create a fresh protocol
session, verify device identity, and avoid replaying uncertain writes. Host SDK
implementation remains outside this firmware repository.

### Attempt 3 observations

The user disconnected the Nano, locked the iPad, waited, and connected the Nano
while the host remained locked. After unlocking and following the authorization
step when applicable, Ethernet appeared automatically and the local page loaded
successfully with green `exact records validated` status. No RESET, repeated
cable reconnect, manual IP change, or other recovery was reported.

This passes the functional locked-then-unlocked scenario. The exact lock-screen
indication, whether an authorization prompt appeared after unlock, elapsed time,
and final record count were not separately reported, so no claim is made for
those details.
