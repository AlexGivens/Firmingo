#!/usr/bin/env python3
"""Combine an exact resident UF2 and one validated module for ROM-only initial setup."""

import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
MAGIC_START = (0x0A324655, 0x9E5D5157)
MAGIC_END = 0x0AB16F30
FAMILY = 0xE48BFF56
SLOT = 0x10200000
SLOT_END = 0x10600000


def sha(data):
    return hashlib.sha256(data).hexdigest()


def checked_resident(path):
    data = path.read_bytes()
    if not data or len(data) % 512:
        raise ValueError("resident UF2 size is invalid")
    blocks = [bytearray(data[i:i + 512]) for i in range(0, len(data), 512)]
    addresses = set()
    for index, block in enumerate(blocks):
        a, b, flags, address, payload, block_number, total, family = struct.unpack_from(
            "<8I", block)
        if (a, b) != MAGIC_START or struct.unpack_from("<I", block, 508)[0] != MAGIC_END:
            raise ValueError("resident UF2 magic is invalid")
        if (flags, payload, family) != (0x2000, 256, FAMILY):
            raise ValueError("resident UF2 profile is invalid")
        if block_number != index or total != len(blocks):
            raise ValueError("resident UF2 block ordering is invalid")
        if not 0x10000000 <= address < 0x10200000 or address % 256:
            raise ValueError("resident UF2 writes outside its reservation")
        if address in addresses:
            raise ValueError("resident UF2 has duplicate addresses")
        addresses.add(address)
    return blocks


def checked_capsule(data):
    if len(data) < 257 or len(data) > SLOT_END - SLOT:
        raise ValueError("module capsule is outside the sketch slot")
    magic, abi, header, board, api, code_at, code_bytes, setup, loop = struct.unpack_from(
        "<4sHHIIIIII", data)
    if (magic, abi, header, board, code_at) != (
            b"FMS1", 1, 64, 0x4F4E414E, SLOT + 256):
        raise ValueError("module header does not match the Nano proof")
    if not 0x10000000 <= api < 0x10200000 or api % 4:
        raise ValueError("module API address is outside resident flash")
    if not code_bytes or len(data) != code_bytes + 256:
        raise ValueError("module code length is invalid")
    if sha(data[256:]) != data[32:64].hex():
        raise ValueError("module SHA-256 mismatch")
    for entry in (setup, loop):
        if not entry & 1 or not code_at <= entry & ~1 < code_at + code_bytes:
            raise ValueError("module entry is outside its code")
    if any(data[64:256]):
        raise ValueError("module padding is not zero")
    return api


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sketch", choices=("blink_a", "blink_b"), required=True)
    parser.add_argument("--resident-dir", type=Path,
                        default=ROOT / "build/managed_sketch/resident")
    parser.add_argument("--module-dir", type=Path,
                        default=ROOT / "build/managed_sketch")
    parser.add_argument("--output-dir", type=Path,
                        default=ROOT / "build/managed_sketch/composite")
    args = parser.parse_args()
    resident = args.resident_dir.resolve()
    module = args.module_dir.resolve() / args.sketch
    resident_report = json.loads((resident / "report.json").read_text())
    module_report = json.loads((module / "report.json").read_text())
    for report in (resident_report, module_report):
        for name, expected in report.get("source_sha256", {}).items():
            source = (ROOT / name).resolve()
            if not source.is_file() or not source.is_relative_to(ROOT) or sha(source.read_bytes()) != expected:
                raise ValueError(f"source changed since build: {name}")
    elf = resident / "artifacts/firmingo_managed_resident.ino.elf"
    uf2 = resident / "artifacts/firmingo_managed_resident.ino.uf2"
    capsule_path = module / (args.sketch + ".fms")
    if (resident_report["status"] != "compile-tested experimental resident; not flashed" or
            resident_report["elf_sha256"] != sha(elf.read_bytes()) or
            resident_report["uf2_sha256"] != sha(uf2.read_bytes())):
        raise ValueError("resident report does not match exact artifacts")
    capsule = capsule_path.read_bytes()
    if (module_report["resident_elf_sha256"] != resident_report["elf_sha256"] or
            module_report["capsule_sha256"] != sha(capsule)):
        raise ValueError("module was not built for this resident")
    api_address = checked_capsule(capsule)
    if module_report["api_address"] != f"0x{api_address:08x}":
        raise ValueError("module report API address differs from capsule")
    blocks = checked_resident(uf2)
    padded = capsule + bytes([0xFF]) * ((-len(capsule)) % 256)
    total = len(blocks) + len(padded) // 256
    for index, block in enumerate(blocks):
        struct.pack_into("<II", block, 20, index, total)
    for offset in range(0, len(padded), 256):
        address = SLOT + offset
        if address + 256 > SLOT_END:
            raise ValueError("module UF2 would exceed sketch slot")
        block = bytearray(512)
        struct.pack_into("<8I", block, 0, *MAGIC_START, 0x2000, address,
                         256, len(blocks), total, FAMILY)
        block[32:288] = padded[offset:offset + 256]
        struct.pack_into("<I", block, 508, MAGIC_END)
        blocks.append(block)
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    target = output / (args.sketch + "-initial-rom-only.uf2")
    target.write_bytes(b"".join(blocks))
    result = {
        "status": "ROM-only initial proof image; not flashed or hardware-verified",
        "board": "nano_rp2040_connect",
        "sketch": args.sketch,
        "resident_uf2_sha256": resident_report["uf2_sha256"],
        "module_capsule_sha256": module_report["capsule_sha256"],
        "composite_uf2_bytes": target.stat().st_size,
        "composite_uf2_sha256": sha(target.read_bytes()),
        "resident_blocks": total - len(padded) // 256,
        "sketch_blocks": len(padded) // 256,
    }
    (output / (args.sketch + "-report.json")).write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
