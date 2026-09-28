# Research and historical notes

This directory holds proposals, old roadmap text, hypotheses, and design context.
It is separate from [measured evidence](../evidence/README.md) and the current
[release contract](../releasing.md). Read it for why decisions were considered,
then check current source, tests, board manifests, and exact firmware-hash
evidence before acting on it.

- [Historical product and protocol notes](roadmap-notes.md): the former long
  README, preserved at the 0.1.0 repository reorganization. Its old development
  version labels, provisional interface sketches, and hardware statements are
  historical and may not describe the current 0.1.0 images.
- [Managed-sketch feasibility](managed-sketch-feasibility.md): pinned-core
  constraints and the Nano proof needed before choosing a persistent sketch
  layout or defining network upload.
- [Nano proof contract](managed-sketch-contract.md): actual `m7exp3` facade,
  scheduling, stream, replacement, and recovery limits; experimental only.
- [Nano layout decision](managed-sketch-nano-layout.md): resident/slot ownership,
  reserved flash ranges, RAM limits, and the managed-sketch update choice.
- [M9 upload extension](../protocol.md#experimental-nano-managed-upload-extension-m9):
  explicit-target FMGO staging, ownership, physical authorization, commit/status
  reconciliation, and interruption semantics; [compile/native record](../evidence/managed-sketch-m9exp1.md).
