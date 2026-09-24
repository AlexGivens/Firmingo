# Evidence index

This directory is the historical test record: dated build reports, command
outputs, JSON counters, screenshot hashes, and narrative reports.
These files are evidence **for the exact firmware hashes and host environments
reported in them**. They are not instructions and do not qualify 0.1.0 builds
merely because the source is related. Public text transcripts have been sanitized:
the tester's home directory, Wi-Fi name, host name, exact board IDs, MAC addresses, and
link-local IPv6 addresses are replaced with consistent placeholders. Synthetic
16-hex board IDs in tests and reports demonstrate formatting only; they are not
flash or smoke targets. Firmware UF2 hashes, byte counts, timings, failure
outcomes, and build settings are unchanged. These sanitized transcripts are not
byte-for-byte originals.

| Topic | Read first | Scope |
| --- | --- | --- |
| Baseline and toolchain | [foundation](foundation.md) | Imported predecessor, pins, initial builds |
| Local-only DHCP and USB startup | [local-only](local-only.md), [USB startup](usb-startup.md) | Gateway omission, attachment experiments |
| Exact source/echo | [echo](echo.md), [macOS cold cycles](macos-cold.md) | Byte checks and limited reconnection counts |
| iPad qualification | [iOS](ios-qualification.md) | Local-only Safari, DHCP, authorization, Wi-Fi coexistence |
| FMGO framing/session | [protocol framing](protocol-framing.md), [session](session.md), [coverage](protocol-coverage.md) | Native vectors and protocol checks |
| Application stream | [integration](tcp-session.md), [hardware](application-hardware.md), [backend](application-backend.md), [diagnostics](application-diagnostics.md), [soak](application-soak.md) | Nano revisions, failures, counters and exact traffic |
| Hardware UART | [UART](uart.md) | Nano loopback, independent Pico peer, overrun limits |
| Raspberry Pi Pico | [Pico port](raspberry-pi-pico.md) | Second board builds and macOS hardware sessions |
| Test harness | [attachment harness](attachment-harness.md) | Host-side harness behavior, not device validation |

The published `.json`, `.log`, and helper-script files retain their
historical names and are indexed by the narrative reports. A packet capture and
seven original screenshots are withheld; their original SHA-256
values remain in the relevant reports. Local originals, when available, are
kept under ignored `private-evidence/originals/` and are never part of a
Git commit or release asset. New release evidence should identify board
revision, exact UF2 SHA-256,
host/OS, cable, command or manual action, expected and observed results. Link it
from this index and [board status](../boards.md); never rewrite earlier failures
as passes. Research hypotheses are separately indexed under
[`docs/research`](../research/README.md).
