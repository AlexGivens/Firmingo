# Nano managed-sketch M8 second failed echo — 2026-09-27

The user reported D13 still blinking on `m8exp2` and presented the selected
Nano as `RPI-RP2`. Pinned `picotool` again read flash ID
`5031503337360009`. `flash_initial.py` regenerated, loaded, fully verified,
and rebooted the `m8exp3` Blink B composite, SHA-256
`86d7a6331b3091d6f5fde9be9caec27de929e4f5bd026dbfd94442a0cca3cfdc`.
FMGO hello reported `0.1.0-m8exp3`, the expected board/device ID, and boot ID
`2a5ad7cf4934d5ad`.

A 256-byte exact echo sent all 276 framed wire bytes but received zero echoed
bytes before its 10-second deadline. Before sending, diagnostics showed
`sketch_epoch:1`, `sketch_acknowledged_epoch:0`, bridge directions disabled,
and `sketch_loop_boundaries:7800`. After the timeout and connection close,
diagnostics showed epoch 2, acknowledged epoch 0, application RX peak 256 and
discarded 256, sketch output peak 0, and the same loop-boundary count. FMGO
remained reachable. The D13 observation on this exact image is pending.

Removing the bridge's hardware-lock-backed atomic read-modify-write calls
did not restore byte transfer. Code inspection of the pinned resident ELF
revealed a separate risk: `Session::dispatch` allocated a 3,348-byte stack
frame, compared with 2,764 bytes in the M7 resident. The loop's shallow
core-0 free-stack sample was about 3,920 bytes. That leaves only about 572
bytes for nested callers at dispatch; it does not by itself prove a stack
overflow, but warrants a direct fix before another hardware attempt.

`m8exp4` moves the bounded response-formatting buffers into each Session
object. Its compiled `Session::dispatch` frame is 2,036 bytes, and the
resident's static RAM increases from 93,944 to 96,720 bytes. Native session
and console tests pass. The exact full-composite SHA-256 is
`545967f1606f0c366c6bba57640ccea68e620c6ad23463534da328cd82722bea`.
It has not been flashed or hardware-tested.
