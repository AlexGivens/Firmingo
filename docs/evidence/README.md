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
| Managed sketch experiment | [initial Nano resident](managed-sketch-initial.md), [m7exp2 first boot](managed-sketch-m7exp2.md), [m7exp3 slot repair and replacement](managed-sketch-m7exp3.md), [m8exp1 failed echo](managed-sketch-m8exp1.md), [m8exp2 diagnostic run](managed-sketch-m8exp2.md), [m8exp3 failed echo and stack finding](managed-sketch-m8exp3.md), [m8exp4 first passing console](managed-sketch-m8exp4.md), [m9exp1 upload build](managed-sketch-m9exp1.md), [m9exp1 hardware](managed-sketch-m9exp1-hardware.md) | Exact experimental ROM images, M7/M8 hardware trials, and M9 build/initial hardware checks |
| Test harness | [attachment harness](attachment-harness.md) | Host-side harness behavior, not device validation |
| Managed-sketch SDK handoff | [M10 firmware contract export](managed-sketch-sdk-handoff.md) | Shared production-checked vectors, checksummed archive and pinned capsule recipe; no SDK/IDE workflow claim |
| Exact resident compiler input | [Experimental resident export](resident-compiler-export.md) | Exact source/report/ELF/UF2 binding and independent validation of two WASM capsules; no hardware execution |
| Managed build profile foundation | [Ordinary managed build](managed-profile.md) | Fresh experimental resident build, ELF/UF2 comparison, committed-source export and compiler metadata import; production/stateful/hardware gates remain open |
| Candidate stateful image | [FMS2 initialization proof](stateful-image-v2.md) | Native guarded RAM lifecycle, independently authored fixture, bounded mutations and ARM object compile; no resident RAM reservation, new recipe or board execution |
| Candidate resident state allocation | [FMS2 ARM link proof](stateful-link-proof.md) | Opt-in 4 KiB BSS arena, actual API/linker facts, separately linked initialized/BSS globals and guarded host initialization; runtime integration, deployment export and board execution remain open |
| FMS2 contract and upload adapter | [Target-2 export/adapter](stateful-upload-contract.md) | Versioned resources and actual ELF/UF2 facts, Session/adapter tests, bounded mutations and opt-in upload build; consumer adoption and hardware qualification remain open |
| Selected Nano FMS2 initial setup | [Pinned ROM installation](nano-fms2-initial.md) | SDK/WASM initial capsule, production-validator composition, full resident/sector ROM readback and matching hello; managed upload/state behavior is separate |
| Selected Nano FMS2 SDK hardware | [iPad/iPhone replacement, reboot and interruptions](nano-fms2-hardware.md) | Two target-2 uploads in one boot per host; separate iPad/iPhone pre-commit disconnect/power-cycle, real withheld commit reply, query-only app-restart recovery and fresh upload; unidentified iPhone prerequisite rejection retained; flash-write power loss and broader production gates remain open |
| Unfinished managed-flash checkpoints | [Procedure](../integration/flash-checkpoint-qualification.md), [exact preparation ledger](nano-flash-checkpoints.json) | Actual signed SDK ROM baseline; two compiled native-only fault residents and paired capsules; manual power-cut and recovery execution pending |
| Candidate stateful lifecycle | [FMS2 boot/replacement](stateful-runtime.md) | Opt-in Nano boot build, exact mapped-slot views, initialization before token publication and native replacement fault injection; no FMS2 network target or board execution |
| SDK Nano initial setup | [Selected ROM setup](nano-sdk-initial.md) | Exact exported resident plus SDK WASM GPIO capsule, full initial UF2 verification before boot and macOS hello; no SDK upload/behavior qualification |
| SDK Nano managed hardware | [iPad/iPhone console and GPIO](nano-sdk-hardware.md) | Exact WASM capsules through signed SDK host, verified stage/one commit/startup per sketch, exact console bytes and manual LED transitions; earlier failures retained |

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
