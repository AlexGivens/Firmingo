#!/usr/bin/env python3
"""Export verified experimental Nano resident facts/artifacts; never connect, flash or qualify hardware."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
from experiments.managed_sketch.build_composite import checked_resident_bytes

ELF_LIMIT = 8 * 1024 * 1024
UF2_LIMIT = 4 * 1024 * 1024
SOURCE_LIMIT = 2 * 1024 * 1024
COMMIT = re.compile(r"[0-9a-f]{40}\Z")
HASH = re.compile(r"[0-9a-f]{64}\Z")


def sha(data):
    return hashlib.sha256(data).hexdigest()


def bounded_file(path, maximum):
    """Capture one nonsymlink regular file without blocking on special files or trusting declared sizes."""
    path = Path(path)
    for parent in (path, *path.parents):
        if parent.is_symlink():
            raise ValueError("symlinked resident export input")
    descriptor = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
    try:
        import stat
        size = os.fstat(descriptor).st_size
        if not stat.S_ISREG(os.fstat(descriptor).st_mode) or not 0 < size <= maximum:
            raise ValueError("resident export input is not a bounded regular file")
        with os.fdopen(descriptor, "rb", closefd=False) as source:
            data = source.read(maximum + 1)
        if len(data) != size or os.fstat(descriptor).st_size != size:
            raise ValueError("resident export input changed or exceeded its bound")
        return data
    finally:
        os.close(descriptor)


def unique_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError("duplicate resident report key")
        value[key] = item
    return value


def git_blob(root, commit, name):
    """Read exact committed build input, excluding path traversal and oversized source objects."""
    if not re.fullmatch(r"[A-Za-z0-9_./-]+", name) or any(p in ("", ".", "..") for p in name.split("/")):
        raise ValueError("unsafe resident source path")
    arguments = ["git", "--no-optional-locks", "--no-replace-objects", "-C", str(root)]
    size = subprocess.run(arguments + ["cat-file", "-s", commit + ":" + name],
                          capture_output=True, check=True, timeout=15)
    if not 0 < int(size.stdout) <= SOURCE_LIMIT:
        raise ValueError("excessive resident source object")
    return subprocess.run(arguments + ["show", commit + ":" + name],
                          capture_output=True, check=True, timeout=15).stdout


def resident_elf(data):
    """Extract one readonly FAPI-v1 table from bounded ELF32 ARM metadata and its flash LOAD bytes."""
    if not 52 <= len(data) <= ELF_LIMIT:
        raise ValueError("resident ELF size is invalid")
    fields = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    ident, kind, machine, version, _, phoff, shoff, flags, ehsize, phsize, phcount, shsize, shcount, _ = fields
    if ident[:8] != b"\x7fELF\x01\x01\x01\x00" or (kind, machine, version, ehsize, phsize, shsize) != (2, 40, 1, 52, 32, 40):
        raise ValueError("resident must be ordinary ELF32 little-endian ARM executable")
    if flags >> 24 != 5 or flags & 0x400 or not 1 <= phcount <= 128 or not 1 <= shcount <= 512:
        raise ValueError("unsupported resident ELF ABI or table limits")

    def extent(offset, count):
        if not 0 <= offset <= len(data) or not 0 <= count <= len(data) - offset:
            raise ValueError("truncated resident ELF extent")
        return data[offset:offset + count]

    extent(phoff, phcount * 32)
    extent(shoff, shcount * 40)
    sections = [struct.unpack_from("<10I", data, shoff + i * 40) for i in range(shcount)]
    segments = [struct.unpack_from("<8I", data, phoff + i * 32) for i in range(phcount)]
    matches = []
    for section in sections:
        if section[1] != 2:
            continue
        if section[9] != 16 or section[5] % 16 or section[5] // 16 > 65536 or not 0 < section[6] < shcount:
            raise ValueError("invalid resident symbol table")
        strings = sections[section[6]]
        if strings[1] != 3 or strings[5] > SOURCE_LIMIT:
            raise ValueError("invalid resident symbol strings")
        names = extent(strings[4], strings[5])
        table = extent(section[4], section[5])
        for offset in range(0, len(table), 16):
            name, address, size, info, other, index = struct.unpack_from("<IIIBBH", table, offset)
            if name >= len(names):
                raise ValueError("invalid resident symbol name")
            end = names.find(b"\0", name)
            if end < 0:
                raise ValueError("unterminated resident symbol name")
            if names[name:end] == b"firmingo_sketch_api":
                if info != 0x11 or other != 0 or size != 40 or not 0 < index < shcount:
                    raise ValueError("invalid global resident API table symbol")
                matches.append((address, index))
    if len(matches) != 1:
        raise ValueError("expected one resident API table symbol")
    address, index = matches[0]
    section = sections[index]
    if address % 4 or not 0x10000000 <= address <= 0x10200000 - 40 or section[1] != 1 or section[2] & 3 != 2:
        raise ValueError("resident API table is not readonly allocated flash")
    relative = address - section[3]
    if relative < 0 or relative + 40 > section[5]:
        raise ValueError("resident API table exceeds its section")
    api = extent(section[4] + relative, 40)
    magic, abi, size, *functions = struct.unpack("<IHH8I", api)
    if (magic, abi, size) != (0x49504146, 1, 40):
        raise ValueError("resident API table does not match FAPI ABI one")
    for function in functions:
        entry = function & ~1
        if (not function & 1 or not (0x10000000 <= entry < 0x10200000 or 0x20000000 <= entry < 0x20042000)
                or not any(s[2] & 6 == 6 and s[3] <= entry < s[3] + s[5] for s in sections)):
            raise ValueError("resident API function is not a Thumb executable-section entry")
    flash = []
    table_mappings = []
    for segment in segments:
        kind, offset, virtual, physical, file_bytes, memory_bytes, permissions, _ = segment
        if kind != 1 or not file_bytes:
            continue
        payload = extent(offset, file_bytes)
        if memory_bytes < file_bytes or not 0x10000000 <= physical < physical + file_bytes <= 0x10200000:
            raise ValueError("resident ELF LOAD bytes exceed resident flash")
        flash.append((physical, payload))
        if virtual <= address and address + 40 <= virtual + file_bytes:
            if physical + address - virtual != address or not permissions & 4 or permissions & ~7 or payload[address - virtual:address - virtual + 40] != api:
                raise ValueError("resident API has inconsistent flash LOAD mapping")
            table_mappings.append(permissions)
    if not flash or len(table_mappings) != 1:
        raise ValueError("resident API lacks a unique flash LOAD mapping")
    return {"apiAddress": address, "residentABI": abi, "apiTableBytes": size,
            "apiTableSHA256": sha(api), "functionAddresses": functions, "loadFlags": table_mappings[0]}, flash


def check_uf2(data, flash):
    """Compare all initialized ELF LOAD bytes to the same resident-only RP2040 UF2 image."""
    blocks = checked_resident_bytes(data)
    image = {struct.unpack_from("<I", block, 12)[0]: bytes(block[32:288]) for block in blocks}
    compared = 0
    for address, payload in flash:
        for offset in range(0, len(payload)):
            position = address + offset
            block = image.get(position & ~255)
            if block is None or block[position & 255] != payload[offset]:
                raise ValueError("resident UF2 differs from ELF flash LOAD bytes")
        compared += len(payload)
    return compared


def verified_resident(resident_dir, root, commit):
    """Bind exact artifacts/report to committed project inputs; retain incomplete historical toolchain provenance."""
    if not COMMIT.fullmatch(commit):
        raise ValueError("select an exact lowercase firmware commit")
    report_bytes = bounded_file(Path(resident_dir) / "report.json", 256 * 1024)
    report = json.loads(report_bytes, object_pairs_hook=unique_object)
    if not isinstance(report, dict):
        raise ValueError("resident report must be an object")
    if report.get("status") != "compile-tested experimental resident; not flashed" or report.get("board") != "nano_rp2040_connect" or report.get("core") != "rp2040:rp2040@6.0.0":
        raise ValueError("select a successful experimental Nano resident report")
    sources = report.get("source_sha256")
    if not isinstance(sources, dict) or not 1 <= len(sources) <= 256:
        raise ValueError("resident source inventory missing or excessive")
    required = ["boards/nano_rp2040_connect/baseline.json", "experiments/managed_sketch/abi.h", "src/core/version.h",
                "experiments/managed_sketch/firmingo_managed_resident/firmingo_managed_resident.ino", "experiments/managed_sketch/build_resident.py"]
    if any(name not in sources for name in required):
        raise ValueError("resident report lacks required source identities")
    tracked = subprocess.run(["git", "--no-optional-locks", "--no-replace-objects", "-C", str(root),
                              "ls-tree", "--name-only", "-r", commit, "--", "src"],
                             capture_output=True, check=True, timeout=15).stdout.decode().splitlines()
    if any(name not in sources for name in tracked if name.endswith((".c", ".cpp", ".h", ".hpp", ".inc", ".S", ".s"))):
        raise ValueError("resident report omits tracked library build inputs")
    ignored = {}
    for name, expected in sources.items():
        if not isinstance(expected, str) or not HASH.fullmatch(expected):
            raise ValueError("invalid resident source hash")
        if name == "src/.DS_Store":
            ignored[name] = expected  # Historical Finder metadata, not a compiler input; never read or export it.
            continue
        if sha(git_blob(root, commit, name)) != expected:
            raise ValueError("resident source does not match selected commit: " + name)
    baseline = json.loads(git_blob(root, commit, required[0]))
    if report.get("fqbn") != baseline["fqbn"] or report.get("upload_target") != {
            "id": 1, "format": "nano-managed-v1", "max_size": 4096, "max_chunk": 1024, "authorization": "physical-d2-gnd"}:
        raise ValueError("resident board/profile requirements differ")
    for field, budget in (("flash_bytes", 1048576), ("static_ram_bytes", 196608)):
        if type(report.get(field)) is not int or not 0 < report[field] <= budget or report.get(field.replace("bytes", "budget_bytes")) != budget:
            raise ValueError("resident memory budget mismatch")
    version = re.search(rb'^#define FIRMINGO_VERSION "([0-9.]+)"$', git_blob(root, commit, required[2]), re.MULTILINE)
    sketch = git_blob(root, commit, required[3])
    if version is None or b'FIRMINGO_VERSION "-m9exp1"' not in sketch:
        raise ValueError("unsupported experimental resident version source")
    elf = bounded_file(Path(resident_dir) / "artifacts/firmingo_managed_resident.ino.elf", ELF_LIMIT)
    uf2 = bounded_file(Path(resident_dir) / "artifacts/firmingo_managed_resident.ino.uf2", UF2_LIMIT)
    if sha(elf) != report.get("elf_sha256") or sha(uf2) != report.get("uf2_sha256"):
        raise ValueError("resident artifact hashes differ from build report")
    api, flash = resident_elf(elf)
    compared = check_uf2(uf2, flash)
    facts = {"schema": 1, "board": "nano_rp2040_connect", "profile": "nano-managed-v1", "backend": "application",
             "firmwareVersion": version[1].decode() + "-m9exp1", "firmwareCommit": commit, "residentELFSHA256": sha(elf),
             "firmwareImageSHA256": sha(uf2), "buildReportSHA256": sha(report_bytes), "apiAddress": api["apiAddress"],
             "residentABI": 1, "targetID": 1, "maximumCapsuleBytes": 4096, "qualification": "experimental",
             "apiTable": api, "comparedFlashBytes": compared, "sourceSHA256": {k: v for k, v in sources.items() if k not in ignored},
             "ignoredHistoricalMetadata": ignored, "hardwareQualification": "not established by export",
             "provenanceLimit": "reported project sources match commit; installed core/toolchain inputs are declared, not fully reproduced or inventoried",
             "exportToolSHA256": sha(Path(__file__).read_bytes()),
             "exportValidatorSHA256": sha((ROOT / "experiments/managed_sketch/build_composite.py").read_bytes())}
    return {"resident.elf": elf, "resident.uf2": uf2, "build-report.json": report_bytes,
            "resident-facts.json": (json.dumps(facts, sort_keys=True, indent=2) + "\n").encode()}


def export_resident(resident_dir, output, commit, root=ROOT):
    """Publish only into a new explicitly selected directory after every source/artifact check succeeds."""
    files = verified_resident(resident_dir, root, commit)
    manifest = {"schema": 1, "purpose": "experimental_resident_compiler_export", "board": "nano_rp2040_connect",
                "profile": "nano-managed-v1", "firmwareCommit": commit,
                "files": [{"path": name, "bytes": len(data), "sha256": sha(data)} for name, data in files.items()],
                "qualification": "experimental structural/source/artifact checks; no installation or hardware qualification"}
    manifest_bytes = (json.dumps(manifest, sort_keys=True, indent=2) + "\n").encode()
    output = Path(output)
    output.mkdir()
    for name, data in {**files, "manifest.json": manifest_bytes}.items():
        with (output / name).open("xb") as destination:
            destination.write(data)
    return {"status": "exported_experimental_resident", "manifestSHA256": sha(manifest_bytes),
            "files": len(files), "firmwareCommit": commit, "hardwareQualified": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resident-dir", required=True, type=Path)
    parser.add_argument("--firmware-commit", required=True)
    parser.add_argument("--output", required=True, type=Path, help="new directory; existing output is refused")
    args = parser.parse_args()
    try:
        print(json.dumps(export_resident(args.resident_dir, args.output, args.firmware_commit), sort_keys=True))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, "Resident export failed: " + str(error) + "\n")


if __name__ == "__main__":
    main()
