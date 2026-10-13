# Experimental exact resident export and compiler validation

2026-10-13 local maintenance checks, macOS arm64. No device connection, flash,
installation or hardware execution occurred. The existing historical M9 resident
was inspected, not rebuilt locally. Its reported project sources match commit
`f17fdf0301256ba48270230cbf213126e70aa8d0`; installed upstream core/toolchain
byte/source closure remains incomplete.

The new `experiments/managed_sketch/export_resident.py` produced four artifact/fact
roles plus schema-1 manifest using:

```sh
python3 experiments/managed_sketch/export_resident.py \
  --resident-dir build/managed_sketch/m9exp1/resident \
  --firmware-commit f17fdf0301256ba48270230cbf213126e70aa8d0 \
  --output build/managed_sketch/compiler-export-20261013-final
.venv/bin/python tools/dev.py test --sanitize
```

Native production tests: 20/20 passed with address/undefined-behavior sanitizers.
Host tests: 191/191 passed, including eight exporter cases and existing composite/
contract checks. Owned non-executable fixtures cover all ELF truncations,
malformed symbols/sections/loads/FAPI, UF2 corruption, source drift/omissions,
report/artifact/budget mismatch, symlink/special/oversized input, deterministic
export and no overwrite. No synthetic fixture was installed or executed.

| Identity/check | Result |
| --- | --- |
| Exact resident ELF | `8539fb1b26f016c0bf8290f8404d40afb785099419de84f120f02ee7c401c06d` |
| Resident-only UF2 | `658ce6e056e88d3f1d0dfdd4a936263676eaee16d4eb2271b3c400a8c2fb95bb` |
| Original report | `23d15a7a52e7e274e04ea57f98545166306368f1ef944ba3c878a4b0ad5ab46f` |
| Export manifest | `5d1d1aa1d7d5d21495485d3ab4c838de48e13eabd35c2804075842cbb0c32d85` |
| API table | `0x10022ca0`, ABI 1, 40 bytes; SHA-256 `601872c1352b5eadb3d4fff8872c8b2a27130c4e0f780209bb573fd5fbe22a5f` |
| Initialized ELF-to-UF2 comparison | 152,840 bytes matched |
| LOAD/API-section permissions | LOAD flags 7 (RWX), API object section readonly |

Firmingo-Compiler imported this explicitly anchored export without copying images
or changing its default catalogue. It compiled GPIO/delay and binary-console
sketches through its actual pinned bounded Clang/LLD WASM service; capsules were
316/392 bytes with 60/136 code bytes. A small owned native helper linked the actual
production `experiments/managed_sketch/image.cpp`, called `validate_image` with
API address `0x10022ca0`, and returned `ImageError::ok` for both. The repeated GPIO
build had identical ELF, capsule, report and receipt bytes. Exact requests,
receipts and hashes are in the Compiler repository's
`docs/evidence/resident-validation.json`.

This establishes structural/source/artifact binding and independent capsule
validation for the constrained subset. It does not establish useful behavior,
Clang-to-resident hardware compatibility, installation evidence or full source
reproduction. The GCC Blink proofs use division requiring an unsupported Clang
runtime helper; the supported sketches do not imply broader Arduino support.
ABI/facade/layout publication, complete upstream provenance, wrapper/device
qualification and Pico managed support remain open.

CI's maintained managed-sketch build now exports its freshly compiled resident
against `GITHUB_SHA`, retains the export/logs and preserves failing pipeline status.
[Run 38378075416](https://github.com/AlexGivens/Firmingo/actions/runs/38378075416)
at `4d53a285a3a39d4355253760a205fdac1a6b811c` passed all ten jobs; all job
logs were inspected. Results confirm 20 sanitizer-native/191 host tests, four
bounded mutation tests, seven pinned board/profile builds and the experimental
resident/four native modules/two composites/contract archive. Existing vendor
warnings are retained. These are compile/test outcomes, not hardware evidence.

The new resident ELF hash is
`5b3116cdcde28f84b86f78cfffb258591455406a1f276b35bb5fd63a6f857304`,
resident-only UF2
`35678c95c495c4e06227a992548be893305cc2375f7a6e561d0a7f66433e6983`,
and export manifest
`3109c3e97c95f07c39d50fc1591dab8b8cc44022fde5aa71c7602e5a7974a78e`.
The API address is `0x10022c9c`, four bytes below the historical build's address.
The exporter again compared 152,840 initialized flash bytes. Reported flash/static
RAM sizes are 134,328/96,672 bytes within the declared engineering budgets.

Downloaded its named retained artifact to ignored Compiler outputs and imported
using the manifest hash from the independently retrieved CI log. Both supported
sketches compiled locally through WASM against this new descriptor; production
`image.cpp` accepted both with `0x10022c9c`. Supplying the historical API address
correctly rejected the new GPIO capsule as `wrong_api`. Exact receipts and
identities are in Compiler's `docs/evidence/ci-resident-validation.json`.
No binary entered committed resources, no image was installed, and this does
not establish reproducible bytes between historical macOS and fresh Linux builds.
