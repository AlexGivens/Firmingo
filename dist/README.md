# Generated release packages

`python3 tools/dev.py package` writes `firmingo-<version>/` and a matching ZIP
archive here. The generated UF2s, manifests, and archive are intentionally
ignored by Git; publish the verified archive as a GitHub release asset after
the release checks in
[`docs/releasing.md`](../docs/releasing.md). Do not commit machine-local binaries
or treat compilation as hardware qualification.
