# Upstream USB startup source snapshots

Source: Earle Philhower Arduino-Pico release **6.0.0**:
https://github.com/earlephilhower/arduino-pico/releases/tag/6.0.0
Archive SHA-256: `d2163a321748be872a29cb99ead739840990d3ddf0935de756eb52f9fc2b5d46`.

`upstream/` preserves the six unmodified files touched by the controlled-startup
patch. Copyright/license headers remain intact (Earle F. Philhower III and
functionpointer); GNU LGPL 2.1 or later. The archive's top-level license is copied
as `LICENSE`. These snapshots are test inputs and provenance, not a second SDK
installation. The firmware build uses the pinned installed SDK with a verified
build-only patch overlay. Native startup tests use these exact source snapshots.

See `patches/arduino-pico-6.0.0-usb-startup/manifest.json` for per-file hashes and
its README/patch for the reason and full changes. Retain upstream licensing when
distributing source or firmware with this dependency.
