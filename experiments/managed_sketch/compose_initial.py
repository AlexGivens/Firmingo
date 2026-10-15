#!/usr/bin/env python3
"""Prepare a ROM-only Nano image from independently selected resident/capsule pins.

Offline only. This does not compile, select a device, flash, or attest installation.
The first 4 KiB managed sector is fully initialized; other slot sectors are unused
by this constrained resident and are not included. Existing native proof reports
and their build_composite.py workflow retain their meaning.
"""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct

if __package__:
    from . import build_composite as uf2
else:
    # Support the documented isolated interpreter without cwd/sys.path imports.
    spec = importlib.util.spec_from_file_location(
        "firmingo_initial_uf2", Path(__file__).resolve().with_name("build_composite.py"))
    uf2 = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(uf2)

SECTOR_BYTES = 4096


def pinned_bytes(path, expected, maximum):
    if not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise ValueError("expected SHA-256 must be 64 lowercase hexadecimal digits")
    with path.open("rb") as source:
        data = source.read(maximum + 1)
    if len(data) > maximum or hashlib.sha256(data).hexdigest() != expected:
        raise ValueError("input size or independently selected SHA-256 differs")
    return data


def compose(resident, capsule, expected_api):
    """Return checked UF2 bytes, preserving every resident payload byte."""
    blocks = uf2.checked_resident_bytes(resident)
    if len(capsule) > SECTOR_BYTES:
        raise ValueError("capsule exceeds the resident's 4096-byte upload limit")
    if uf2.checked_capsule(capsule) != expected_api:
        raise ValueError("capsule does not bind the selected resident API address")
    sector = capsule + b"\xff" * (SECTOR_BYTES - len(capsule))
    total = len(blocks) + SECTOR_BYTES // 256
    for index, block in enumerate(blocks):
        struct.pack_into("<II", block, 20, index, total)
    for offset in range(0, SECTOR_BYTES, 256):
        block = bytearray(512)
        struct.pack_into("<8I", block, 0, *uf2.MAGIC_START, 0x2000,
                         uf2.SLOT + offset, 256, len(blocks), total, uf2.FAMILY)
        block[32:288] = sector[offset:offset + 256]
        struct.pack_into("<I", block, 508, uf2.MAGIC_END)
        blocks.append(block)
    return b"".join(blocks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resident-uf2", type=Path, required=True)
    parser.add_argument("--expected-resident-sha256", required=True)
    parser.add_argument("--capsule", type=Path, required=True)
    parser.add_argument("--expected-capsule-sha256", required=True)
    parser.add_argument("--expected-api-address", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--output-dir", type=Path, required=True,
                        help="new directory only; never overwrite prepared evidence")
    args = parser.parse_args()
    resident = pinned_bytes(args.resident_uf2, args.expected_resident_sha256, 4 * 1024 * 1024)
    capsule = pinned_bytes(args.capsule, args.expected_capsule_sha256, SECTOR_BYTES)
    image = compose(resident, capsule, args.expected_api_address)
    result = {
        "schema": 1,
        "status": "prepared ROM-only experimental image; not flashed or hardware-qualified",
        "board": "nano_rp2040_connect",
        "residentUF2SHA256": uf2.sha(resident),
        "capsuleSHA256": uf2.sha(capsule),
        "capsuleBytes": len(capsule),
        "apiAddress": f"0x{args.expected_api_address:08x}",
        "compositeUF2SHA256": uf2.sha(image),
        "compositeUF2Bytes": len(image),
        "residentBlocks": len(resident) // 512,
        "managedSector": {"address": "0x10200000", "bytes": SECTOR_BYTES,
                          "padding": "0xff after capsule"},
        "effect": "Replace resident payload regions and initialize first managed sector; other flash remains outside this image",
        "hardwareQualified": False,
    }
    args.output_dir.mkdir(parents=True, exist_ok=False)
    (args.output_dir / "initial.uf2").write_bytes(image)
    (args.output_dir / "report.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
