#!/usr/bin/env python3
"""Export experimental FMGO documentation and non-executable SDK test vectors."""

import argparse
import hashlib
import io
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def contract_files(root):
    paths = [Path("docs/protocol.md"), Path("docs/integration/managed-sketch-sdk.md"),
             Path("experiments/managed_sketch/abi.h"),
             Path("experiments/managed_sketch/image.h")]
    paths += sorted(path.relative_to(root) for path in
                    (root / "tests/fixtures/protocol-v1").rglob("*") if path.is_file())
    return {str(path): (root / path).read_bytes() for path in sorted(paths)}


def export_contract(output, root=ROOT):
    files = contract_files(root)
    manifest = {
        "bundle_schema": 1,
        "status": "experimental SDK contract and test vectors; no firmware images",
        "protocol_major": 1,
        "reference_resident_version": "0.1.0-m9exp1",
        "fixtures_are_executable": False,
        "files": {name: {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                  for name, data in files.items()},
    }
    files["manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, data)
    data = buffer.getvalue()
    # The export is a new artifact, never an implicit overwrite of existing work.
    with Path(output).open("xb") as destination:
        destination.write(data)
    return {"output": str(Path(output).resolve()), "files": len(files),
            "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path,
                        help="new ZIP path; parent must exist; existing files are refused")
    args = parser.parse_args()
    try:
        result = export_contract(args.output)
    except OSError as exc:
        parser.exit(1, f"Export failed: {exc}\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
