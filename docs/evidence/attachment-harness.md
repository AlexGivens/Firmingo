# Attachment harness development — 2026-09-18

Added `tools/dev.py attach`, a macOS-only read-only firmware qualification
operation. It selects one Nano USB/diagnostic ID and interface, requires observed
USB disappearance and a new registry instance, waits for local DHCP/address,
checks routes/identity/startup ordering, then verifies concurrent exact source,
echo/reconnect and fresh Internet access. It saves atomic stage checkpoints,
refuses existing output and stops at the first failure. It performs no reset,
flash, host network edits or automatic test retries. See
[operation, command and measurement limits](../cold-attachment.md).

The prior cold cycle lacked insertion/lease timing. This tool adds explicit
host-observed USB-to-address and USB-to-identity intervals with sample boundaries.
They do not measure physical insertion or packet-level DHCP timing. Authorization,
physical power removal, board revision and cable/hub remain user observations.
A manually selected UF2 hash is metadata, not live flash verification.

Commands actually run:

```sh
.venv/bin/python -m pytest tests/host/test_attach.py -q
.venv/bin/python tools/dev.py test --sanitize
.venv/bin/python tools/dev.py attach --help
```

The focused suite has 21 passing simulated cases. It exercises selected-device
filtering, duplicate identity rejection, wrong/unsafe DHCP, observation deadlines,
command errors versus absence, invalid startup diagnostics, stage checkpoints,
preserving old evidence, missing user action, byte failure without retry and route
mismatch before traffic. These are host-harness tests, not USB hardware evidence.

No firmware sources, descriptors, DHCP or on-board image were changed for this
operation. Therefore no new firmware compile/flash was needed. The selected Nano
is still present as NCM-only on en5 with ID a1b2c3d4e5f60718. Structured USB registry
reading worked with the desktop tool's host-observation permission; restricted
sandbox access alone denied archive output. This was not an automatic-approval
rejection or board failure.

The full regression passed all five native suites with address/undefined-behavior
sanitizers and all 60 host tests. The local output is in
`build/attach-regressions.log`. Before arming the first timed check, physical macOS qualification was
1 functional pass / 1 attempt toward 20, with that cycle's
lease latency unmeasured. Adding a watcher does not add cold-connection passes.
The subsequent real timed cycle 2 passed; its evidence and current counts are in
the [cold-cycle ledger](macos-cold.md).
