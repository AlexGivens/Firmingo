# Nano managed-sketch M8 diagnostic run — 2026-09-27

This is a failed experimental hardware observation on the previously selected
Nano RP2040 Connect, flash ID `5031503337360009`. It does not establish M8
console support.

The user reported D13 still blinking on `m8exp1`, then presented the board as
`RPI-RP2`. Pinned `picotool info -a` again reported RP2040 B1, 16 MiB flash,
and the expected flash ID. `flash_initial.py` regenerated, loaded, fully
verified, and rebooted the exact `m8exp2` Blink B composite (SHA-256
`b437696563b5e64380dc32542dc264e3439fca71cdb55d5f8bd4edc314c76bf9`).
FMGO hello reported `0.1.0-m8exp2`, the expected board/device ID, and boot ID
`9735041156351825`. The user later reported D13 was still blinking before
returning the board to ROM mode.

`device.diagnostics` before opening the stream showed
`sketch_loop_boundaries:7880`, `sketch_epoch:0`, and
`sketch_acknowledged_epoch:0`. After `serial.open`, it showed epoch 1 but
acknowledged epoch 0, with both bridge directions disabled. One second after
sending a single `Z` byte, `serial.status` reported
`backend_rx_bytes:1`, while diagnostics showed
`application_rx_pending:1`, `sketch_output_peak:0`, and the same 7880 loop
boundaries. Another read two seconds later also reported 7880 boundaries.
The board remained reachable over FMGO throughout. The sketch loop counter
stopped advancing before the new owner could be acknowledged, but the later
visual D13 observation shows that the counter alone cannot establish that
core 1 stopped. The reason for this discrepancy is unresolved.

The `m8exp2` diagnostic loop increment used `std::atomic` read-modify-write
once per sketch loop. The pinned RP2040 build links this operation to a shared
hardware spinlock. Each bridge counter and epoch has only one writer core, so
`m8exp3` replaced its unnecessary atomic read-modify-write operations with
single-writer atomic loads/stores. This was a candidate fix, not an established
cause. Native and sanitizer tests passed; the pinned build contains no
`__atomic_compare_exchange_4` or `__atomic_fetch_add_4` symbol. The exact
`m8exp3` composite SHA-256 is
`86d7a6331b3091d6f5fde9be9caec27de929e4f5bd026dbfd94442a0cca3cfdc`.
It was flashed and the 256-byte echo also timed out; see the
[m8exp3 record](managed-sketch-m8exp3.md).
