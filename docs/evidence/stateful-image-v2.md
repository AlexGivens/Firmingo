# Candidate FMS2 initialization proof — 2026-10-15

Source `831f0f996cbfb7733aa6469804c6b1ba087c8a71` implements the
[candidate image/lifecycle contract](../integration/stateful-image-v2.md).
[Exact records](stateful-image-v2.json) bind committed sources, independent fixture,
actual local logs and the ARM object. Existing unrelated `AGENTS.md` edits remain
excluded. No board, USB, flash or installed firmware operation occurred.

The ordinary sanitizer entry point passes **21/21 CTest targets and 217 host
cases**. The final focused candidate run passes ten behavior cases, including
additional aligned-overflow, no-flash-room-for-data and both alias directions.
These use a Python `struct`/`hashlib` golden; its synthetic API/state mapping and
Thumb bytes are never offered for deployment or executed.

Assertions exercise exact-span parsing, header/payload integrity, all truncated
lengths, recomputed-hash metadata corruption, RAM/data/BSS bounds, zero sections,
minimum/maximum images, guarded initialization, active-state preservation,
explicit stop, smaller replacements, failure without writes and token exhaustion.
The fixture's mutable host RAM models loop-state retention; it does not prove
that an independently linked ARM sketch accesses those variables correctly.

All five maintained bounded mutation targets pass with sanitizers. The final
candidate binary runs 20,000 fixed-seed cases (`0x46534d32`): **368 accepted and
19,632 rejected**. Accepted inputs must initialize only the owned window;
rejections must preserve all bytes, inactive state and zero result metadata.
Both outcomes retain guard bytes. This is bounded mutation coverage, not an
exhaustive malformed-input or machine-code safety proof.

Pinned GCC compiles the candidate as freestanding C++11 for Cortex-M0+ with
warnings as errors and no diagnostics. Its output is an ARM ELF32 relocatable
object, not a linked resident/module. CI now runs the candidate native tests,
separate mutation job and object check; local YAML/bash checks pass. The subsequent
GitHub result remains separate from these inspected local outputs.

The [run at `a7966c5`](https://github.com/AlexGivens/Firmingo/actions/runs/38720627833)
reports all ten jobs successful. Public job and step statuses were inspected;
raw remote logs/artifacts were not retrieved, so remote test counts and binary
bytes are not claimed from those statuses. The JSON retains the run and jobs.

A read-only inspection of the existing local ABI-one resident's ELF also records
its actual linker symbols for follow-up allocation work: `__end__` at
`0x200179a0`, heap limit `0x20040000`, and 2048-byte linker stack reservations on
each core in the scratch banks. The 165,472-byte nominal end-to-heap-limit gap is
not measured free RAM. Heap use, stack peaks and a new state reservation remain
unmeasured; no candidate address or capacity was selected from this inspection.

Real resident RAM allocation/budget, API/linker export, independently linked
stateful ARM modules, compiler/SDK adoption and physical qualification remain
open. Current FMS1/ABI-one behavior, exported contracts and consumer pins remain
unchanged. See [the 1.0 plan](../integration/production-v1.md) for the next gates.
