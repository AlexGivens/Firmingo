# Resident source-contract integration — 2026-10-13

The existing exact experimental resident export now includes a schema-one source
contract within export schema 2. It exports ABI/image headers, native proof facade,
entry and linker, and the Firmingo license from the selected Git commit. Resident
headers must match the actual build report; all source objects must be regular Git
blobs. Unknown ABI/image layouts fail before output creation. This introduces no
runtime firmware, API-table, image format or wire-protocol change.

Actual local export:

```sh
python3 experiments/managed_sketch/export_resident.py \
  --resident-dir ../Firmingo-Compiler/.build/ci-firmware-export/managed_sketch/ci/resident \
  --firmware-commit 4d53a285a3a39d4355253760a205fdac1a6b811c \
  --output build/managed_sketch/contract-schema2-review
```

The explicit input here is the previously verified firmware CI build, not a
sibling discovered by ordinary tests. Eleven exported roles and `manifest.json`
are produced; manifest SHA-256 is
`4992c149d1b0a31c7ad6e5e3f2e43adbb19bb429bfb95e60155a04f5e17de84a`.
The source contract has identifier `firmingo-nano-managed-abi1-v1-experimental`.
Its exact six source/license identities match the reviewed compiler mapping.

Compiler schema-2 import passes with the same descriptor as the prior schema-1
import: build ID
`760c563a3288d9e07fbe7c501c173516736913324db760e4152640ea45dc7e8d`,
API `0x10022c9c`. The compiler adds its own pinned recipe identity and a verified
source-contract receipt; the exporter assigns no recipe or device identity.
The metadata-only candidate inventory binds the actual successful owned compiler
build receipt and source capture to that contract. It copies no compiler binary
or firmware image and leaves source-package/pins/catalogue unchanged.

Actual source-built WASM checks using the imported schema-2 descriptor produce
GPIO/delay and binary-console capsules identical to the preceding Node results,
with three unsupported builds rejected and fresh/repeat checks passing. The
independently compiled actual firmware `image.cpp/image.h` validator at `d42613f`
accepts both at `0x10022c9c` and rejects both at `0x10022ca0` (`wrong_api`, enum 6).
No Thumb code executes and no board, upload, reset or flash operation occurs.
Full cross-repository identities and outcomes are recorded in Firmingo-Compiler's
`docs/evidence/candidate-contract-inventory.md` and `.json`.

Local checks actually run:

- `python tools/dev.py test --sanitize`: 20 native sanitizer cases and 193 host
  cases passed before the final two regression cases were added.
- `python -m pytest tests/host/test_resident_export.py -q`: all 12 final exporter
  cases passed, including ABI field-width drift and committed Git symlinks.
- `python -m pytest tests/host -q`: all 195 final host cases passed.
- Compiler ordinary tests: 132 passed, one optional-service skip. Nine focused
  importer/candidate cases and syntax/resource checks passed.
- Workflow YAML parses. Managed firmware CI is updated to upload every nested
  export resource; subsequent GitHub results require inspection before claims.

No new local firmware build ran; firmware source and dependency pins did not
change. CI continues the maintained board/profile and experimental build matrix.
The user's existing `AGENTS.md` edits are preserved independently of this change.
Upstream firmware core/toolchain reproduction, custom compiler source licensing,
package promotion, real SDK/private-scheme and hardware qualification remain open.
