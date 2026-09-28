#!/usr/bin/env python3
"""Build one experimental Nano sketch capsule using the pinned ARM compiler."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
CODE_ADDRESS = 0x10200100
SLOT_BYTES = 4 * 1024 * 1024
NANO_TAG = 0x4F4E414E
TOOL_VERSION = "5.0.0-9576866"


def run(command):
    print("+", " ".join(map(str, command)), flush=True)
    subprocess.run([str(item) for item in command], check=True, cwd=ROOT,
                   timeout=120)


def symbol(nm, elf, name, types="Tt"):
    result = subprocess.run([str(nm), "-g", "--defined-only", str(elf)],
                            check=True, capture_output=True, text=True,
                            timeout=30)
    matches = re.findall(r"^([0-9a-fA-F]+)\s+[" + types + r"]\s+" + re.escape(name) + r"$",
                         result.stdout, re.MULTILINE)
    if len(matches) != 1:
        raise RuntimeError(f"expected one {name} symbol in {elf}")
    return int(matches[0], 16)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sketch", choices=("blink_a", "blink_b", "pause_probe",
                                             "stall_probe"),
                        required=True)
    parser.add_argument("--resident-elf", required=True, type=Path,
                        help="exact experimental resident ELF containing its API table")
    parser.add_argument("--toolchain-bin", required=True, type=Path,
                        help="bin directory of pinned pqt-gcc/5.0.0-9576866")
    parser.add_argument("--output-dir", type=Path,
                        default=ROOT / "build/managed_sketch",
                        help="generated artifacts; default build/managed_sketch")
    args = parser.parse_args()
    tool_bin = args.toolchain_bin.resolve()
    if tool_bin.name != "bin" or tool_bin.parent.name != TOOL_VERSION:
        parser.error(f"--toolchain-bin must be from pqt-gcc/{TOOL_VERSION}/bin")
    compiler = tool_bin / "arm-none-eabi-g++"
    objcopy = tool_bin / "arm-none-eabi-objcopy"
    nm = tool_bin / "arm-none-eabi-nm"
    if not all(path.is_file() for path in (compiler, objcopy, nm)):
        parser.error("pinned compiler, objcopy, or nm is missing")
    resident_elf = args.resident_elf.resolve()
    if not resident_elf.is_file():
        parser.error("--resident-elf must name an existing ELF")
    api_address = symbol(nm, resident_elf, "firmingo_sketch_api", "Rr")
    if not 0x10000000 <= api_address < 0x10200000 or api_address % 4:
        parser.error("resident API table is outside aligned resident flash")

    output = args.output_dir.resolve() / args.sketch
    output.mkdir(parents=True, exist_ok=True)
    sketch = HERE / (args.sketch + ".ino")
    common = [
        "-mcpu=cortex-m0plus", "-mthumb", "-Os", "-std=gnu++17",
        "-ffreestanding", "-fno-builtin", "-fno-exceptions", "-fno-rtti",
        "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
        "-fno-stack-protector", "-ffunction-sections", "-fdata-sections",
        "-Wall", "-Wextra", "-Werror", "-I" + str(HERE / "facade"),
        f"-DFIRMINGO_API_ADDRESS={api_address}",
    ]
    objects = []
    for source in (sketch, HERE / "module_entry.cpp"):
        target = output / (source.stem + ".o")
        run([compiler, *common, "-x", "c++", "-c", source, "-o", target])
        objects.append(target)
    elf = output / (args.sketch + ".elf")
    run([compiler, "-mcpu=cortex-m0plus", "-mthumb", "-nostdlib",
         "-Wl,--gc-sections", "-Wl,--no-undefined", "-Wl,--build-id=none",
         "-Wl,-e,firmingo_sketch_setup", "-Wl,-T," + str(HERE / "module.ld"),
         "-Wl,-Map," + str(output / (args.sketch + ".map")), *objects,
         "-lgcc", "-o", elf])
    code_path = output / (args.sketch + ".bin")
    run([objcopy, "-O", "binary", elf, code_path])
    code = code_path.read_bytes()
    if not code or len(code) > min(SLOT_BYTES, 4096) - 256:
        raise RuntimeError(f"module size {len(code)} exceeds the 4 KiB install capsule limit")
    setup = symbol(nm, elf, "firmingo_sketch_setup") | 1
    loop = symbol(nm, elf, "firmingo_sketch_loop") | 1
    for name, address in (("setup", setup), ("loop", loop)):
        if not CODE_ADDRESS <= address & ~1 < CODE_ADDRESS + len(code):
            raise RuntimeError(f"{name} address is outside generated code")
    digest = hashlib.sha256(code).digest()
    header = struct.pack("<4sHHIIIIII32s", b"FMS1", 1, 64, NANO_TAG,
                         api_address, CODE_ADDRESS, len(code), setup, loop,
                         digest)
    assert len(header) == 64
    capsule = header + bytes(256 - len(header)) + code
    capsule_path = output / (args.sketch + ".fms")
    capsule_path.write_bytes(capsule)
    report = {
        "status": "compile-tested module only; no resident or hardware proof",
        "board": "nano_rp2040_connect",
        "sketch": args.sketch,
        "image_format": "nano-managed-v1",
        "target_id": 1,
        "max_install_bytes": 4096,
        "api_address": f"0x{api_address:08x}",
        "resident_elf_sha256": hashlib.sha256(resident_elf.read_bytes()).hexdigest(),
        "setup_address": f"0x{setup:08x}",
        "loop_address": f"0x{loop:08x}",
        "code_bytes": len(code),
        "code_sha256": digest.hex(),
        "capsule_bytes": len(capsule),
        "capsule_sha256": hashlib.sha256(capsule).hexdigest(),
        "source_sha256": {
            str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (sketch, HERE / "build_module.py", HERE / "abi.h", HERE / "facade/Arduino.h",
                         HERE / "module_entry.cpp", HERE / "module.ld")
        },
        "compiler": str(compiler),
    }
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
