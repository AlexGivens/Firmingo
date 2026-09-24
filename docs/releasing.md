# Building and packaging 0.1.0 beta

`src/core/version.h` is the single firmware version source. `library.properties`
must match it; the build and packaging tools reject a mismatch. The version is
`0.1.0` and its release channel is **beta**. The FMGO wire protocol has its own
major version and remains a development contract.

## Local release layout

Generated packages are ignored by Git. `python3 tools/dev.py package` creates:

```text
dist/
  README.md                         # tracked instructions
  firmingo-0.1.0/                   # generated; upload only after checks
    README.md
    manifest.json
    nano_rp2040_connect/
      application/
        firmingo-0.1.0-nano_rp2040_connect-application.uf2
      uart/
        firmingo-0.1.0-nano_rp2040_connect-uart.uf2
    raspberry_pi_pico/
      application/
        firmingo-0.1.0-raspberry_pi_pico-application.uf2
      uart/
        firmingo-0.1.0-raspberry_pi_pico-uart.uf2
  firmingo-0.1.0.zip               # generated archive retaining this tree
```

Each `files[]` manifest entry has exact `board_id`, `profile`,
`firmware_version`, UF2 `bytes` and `sha256`, FQBN, compiled flash/static-RAM
size, `qualification`, and the build report SHA-256. The top-level index declares
schema 1, product, version, channel, and UF2 format. The filename and manifest
are both board-specific; an SDK must match them to the selected physical board
and verify size/hash before flashing. The manifest describes built artifacts; it
is **not** a signature or device authorization scheme. It does not prove that a
specific board is currently running that image.

## Reproduce

Use the pinned setup in [development](development.md), then:

```sh
.venv/bin/python tools/dev.py test --sanitize
.venv/bin/python tools/dev.py build --board nano_rp2040_connect --firmware application
.venv/bin/python tools/dev.py build --board raspberry_pi_pico --firmware application
.venv/bin/python tools/dev.py build --board nano_rp2040_connect --firmware uart
.venv/bin/python tools/dev.py build --board raspberry_pi_pico --firmware uart
.venv/bin/python tools/dev.py package
```

By default both application and UART profiles are included. Use
`package --firmware application` for an application-only package. Repeat `--board`
to select an explicit subset. The command verifies current source hashes, exact
board manifest, build status, UF2 name/size/hash, and flash/static-RAM budgets
before writing any release file. It refuses to overwrite an existing package;
move or remove that local generated directory before intentionally repackaging.
The matching ZIP uses fixed entry timestamps and includes the whole versioned
directory, so GitHub can distribute the tree as one release asset. Verify the
archive contents and extracted manifest before publishing.
Run `python3 tools/dev.py package --help` for current options.

## Publication gates

A local package is **compile-tested**, not automatically hardware-qualified.
For a GitHub release, record the exact packaged UF2 hashes, board revisions,
core/tool versions, and results of [hardware smoke](testing.md) and
[iOS qualification](ios-qualification.md) for every claimed target. Check cold
attachment, authorization, DHCP with no router/DNS advertisement, exact stream
bytes, reconnect behavior, and simultaneous uncached Internet access. Do not
claim a profile or board that lacks direct evidence; Pico UART and FMGO on iOS
remain open. Preserve failures and the test environment in
[evidence](evidence/README.md). The open-development authentication model is
not a production security release. Publish the package ZIP through GitHub releases
only after a deliberate release decision; this tool does not publish or flash.
