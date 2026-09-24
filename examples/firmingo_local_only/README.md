# RP2040 local-only network qualification

Current source identifies as Firmingo **0.1.0 beta**, but its exact rebuilt UF2
has not yet received fresh hardware qualification. The macOS and iPad results
below refer to earlier `ncm-startup-v1` development images and their recorded
hashes in [evidence](../../docs/evidence/README.md). This profile is deliberately
excluded from the default release package.

This fork uses the repository-vendored local-only DHCP server and portable stream
core with a nonblocking lwIP echo adapter. USB controller startup is deferred
until network services are ready, using the pinned NCM-only core patch. Default board IP: `192.168.77.1/24`;
no DHCP router or DNS options. macOS source/echo/Internet checks pass, including
256 KiB exact echo. Three staged local-only iPad attachment scenarios pass;
the full repeated-attachment target remains incomplete.

This source is also compile-tested for `raspberry_pi_pico`. One Pico macOS session
passes BOOTSEL flash, automatic NCM/DHCP, identity, exact source/echo/reconnect,
and concurrent IPv4/IPv6 Internet access; repeated attachment and iOS remain
unverified on the Pico.

See [build and validation](../../docs/local-only-network.md) and
[echo contract, results and limits](../../docs/evidence/echo.md).

Arduino CLI builds require `--library` pointing to the repository; the documented
`tools/dev.py build --firmware local-only` supplies it. Source/echo ports remain
5000/5001, HTTP 80. Echo permits one owner; additional clients receive TCP reset.
Disconnect discards pending bytes; half-close is unsupported. Idle expiry is 30 s.
Source/HTTP retain prototype behavior and still need bounded I/O work.
`/diagnostics` reports stable board ID and startup timestamps. No CDC reset path
is exposed: use REC/GND ROM recovery before another flash. See
[USB startup qualification](../../docs/usb-startup.md). No production stream/control
contract is implemented.

The HTTP page validates the complete deterministic record format in Safari:
magic, continuous sequence, length/flags, and every payload byte. Browser stream
chunks may split or combine records. A green increasing validated-record count is
exact board-to-browser evidence for this qualification service; it does not test
the production FMGO protocol or arbitrary browser-to-board binary payloads.
