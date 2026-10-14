# Versioned resident source contract

`export_resident.py` now emits **export schema 2**. Existing exact artifact/report
checks remain, and the export adds `contract.json` plus six resources read from the
selected Git commit rather than the current worktree:

| Export path | Original source | Role |
| --- | --- | --- |
| `contract/abi.h` | `experiments/managed_sketch/abi.h` | Actual resident build input |
| `contract/image.h` | `experiments/managed_sketch/image.h` | Actual resident build input |
| `contract/facade/Arduino.h` | `experiments/managed_sketch/facade/Arduino.h` | Native proof reference |
| `contract/module_entry.cpp` | `experiments/managed_sketch/module_entry.cpp` | Native proof reference |
| `contract/module.ld` | `experiments/managed_sketch/module.ld` | Native proof reference |
| `contract/LICENSE` | `LICENSE` | Firmingo source license |

The export contains eleven inventoried files plus `manifest.json`; original facade
include layout is preserved. Each resource records its source path, role, size and
SHA-256. Resources must be committed regular Git blobs and at most 64 KiB. The ABI
and image headers must also appear with matching hashes in the actual resident
build report. Native proof inputs are explicitly separate from resident inputs;
they are not a consumer WASM compiler or recipe.

Source-contract schema 1 is identified as
`firmingo-nano-managed-abi1-v1-experimental`. The exporter checks the known ABI-one
field/function order and derives image constants from the selected header, failing
closed for unsupported layouts. The contract records a 4 MiB slot, 256-byte code
offset, 64-byte header and 4 KiB maximum install capsule, leaving 3,840 code bytes.
The native proof linker describes a larger region; that region does not grant a
larger installable capsule. Firmware validation and the compiler's stricter recipe
remain authoritative for their respective checks.

The current compatibility policy requires the exact selected resident API address.
If that address changes, existing capsules are rejected and must be rebuilt for
the new binding. Any changed source resource requires explicit compiler recipe
review even if its ABI number remains one. An unchanged identifier alone is not
permission to accept new bytes. Broader layouts, mutable storage or compiler helper
support require deliberate work and qualification. Matching an address or hash
does not authenticate a device or prove its installed image.

The CLI remains:

```sh
python3 experiments/managed_sketch/export_resident.py --resident-dir exact/build \
  --firmware-commit EXACT_40_CHARACTER_COMMIT --output new/export-directory
```

New exports require a schema-2-aware importer. Firmingo-Compiler accepts archived
schema-1 exports for metadata compatibility but labels their source contract
unverified; its candidate inventory requires schema 2. The compiler compares all
six resource identities to a reviewed contract-to-recipe mapping and attaches its
own recipe. Descriptor/build identities, firmware ABI and wire protocol remain
unchanged. The default catalogue and SDK package remain unchanged.

Local tests cover repeat output, dirty-worktree isolation, missing build identity,
ABI/layout drift, Git symlinks, unsafe/bounded inputs and existing ELF/UF2 checks.
Normal host CI exercises those cases; managed-build CI now uploads the complete
nested export, including all source/license resources. Actual source-built WASM
checks against the imported CI resident pass separately; see
[the recorded integration](../evidence/resident-source-contract.md).

The existing core/toolchain report remains declared input provenance, not a full
upstream source inventory or reproduction. These contracts supply exact project
resources; they do not close upstream provenance, release or hardware gates.
