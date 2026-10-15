# FMS2 target-2 protocol fixtures

These independently encoded FMGO-major-1 frames select `target_id:2` and
`nano-managed-v2`. The capsule is the existing non-executable FMS2 golden fixture,
with synthetic API `0x10000100`, synthetic RAM mapping `0x20030000`/32 bytes and
SHA-256 `1c955e832d351a9b91a3bd5e5e4463a2f6732e4e87cd1de0d783c769f01ae867`.
Never deploy it or treat its mapping as resident build facts.

Python `struct`, `json` and `hashlib` were used independently of the firmware
encoder/parser. Frame headers and upload ID/offset are big-endian; the capsule
header itself is little-endian. Expected response shapes preserve FMGO version 1.
`old-target-request` / `wrong-target-response` establish explicit rejection of
ABI one's target/format. Other names follow the ABI-one fixture directory;
accepted, verified, committing, installed and boot-confirmed remain separate.

Native tests feed one-byte reads through production Session and compare complete
frames while using the real FMS2 adapter/runtime/replacement driver, fake flash
and native observation callbacks. These are wire/state tests, not hardware proof.
The original ABI-one fixtures remain unchanged.
