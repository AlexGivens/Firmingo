# FMS2 native-only fixture

`initialized.hex` is a 272-byte independently authored candidate capsule. Python's
`struct.pack_into('<4sHH14I', ...)` wrote the documented fields; `hashlib.sha256`
computed the payload/header digests. The C++ parser and hash implementation did
not generate it. `provenance.json` records the exact whole-file and digest values.

The synthetic API is `0x10000100`, state base `0x20030000`, data/BSS sizes eight
bytes each, data load offset 264, setup/loop `0x10200101`/`0x10200105`. Flags and
reserved/padding bytes are zero. The eight code bytes are `00 bf 70 47 00 bf 70 47`;
initial data is little-endian words `0x11223344` and `0xaabbccdd`. Payload hash is
SHA-256 of bytes 256 onward; header hash is SHA-256 of bytes 0–95 inclusive.

Native tests map an owned 32-byte host array to the synthetic target address,
surrounded by guards. These addresses are not resident facts or reserved MCU
memory. Never deploy this fixture or derive a board descriptor from it. The
synthetic code is not executed. See [the candidate contract](../../../docs/integration/stateful-image-v2.md).
