#!/usr/bin/env python3
"""Build an opt-in Nano RAM allocation and native ARM FMS2 proof; never flash."""

import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
from experiments.managed_sketch.export_resident import (
    ELF_LIMIT, UF2_LIMIT, bounded_file, check_uf2, resident_elf, sha, unique_object,
)

CODE_ADDRESS = 0x10200100


class Elf:
    """Small, bounded ELF32 ARM reader for this native proof, not a compiler SDK."""

    def __init__(self, data):
        self.data = data
        if not 52 <= len(data) <= ELF_LIMIT:
            raise ValueError("ELF size outside proof limit")
        fields = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
        ident, kind, machine, version, _, phoff, shoff, flags, ehsize, phsize, phcount, shsize, shcount, strings = fields
        if (ident[:8] != b"\x7fELF\x01\x01\x01\x00"
                or (kind, machine, version, ehsize, phsize, shsize) != (2, 40, 1, 52, 32, 40)
                or flags >> 24 != 5 or flags & 0x400
                or not 1 <= phcount <= 128 or not 1 <= shcount <= 512):
            raise ValueError("expected bounded ELF32 ARM EABI5 executable")
        self.extent(phoff, phcount * 32)
        self.extent(shoff, shcount * 40)
        self.sections = [struct.unpack_from("<10I", data, shoff + i * 40) for i in range(shcount)]
        self.segments = [struct.unpack_from("<8I", data, phoff + i * 32) for i in range(phcount)]
        if not 0 < strings < shcount or self.sections[strings][1] != 3:
            raise ValueError("missing section names")
        names = self.section_bytes(self.sections[strings])
        self.named = {}
        for section in self.sections[1:]:
            name = self.string(names, section[0])
            if name in self.named:
                raise ValueError("duplicate ELF section name")
            self.named[name] = section
        self.symbols = {}
        for section in self.sections:
            if section[1] != 2:
                continue
            if section[9] != 16 or section[5] % 16 or section[5] // 16 > 65536 or not 0 < section[6] < shcount:
                raise ValueError("invalid symbol table")
            strings_section = self.sections[section[6]]
            if strings_section[1] != 3:
                raise ValueError("invalid symbol names")
            names = self.section_bytes(strings_section)
            table = self.section_bytes(section)
            for offset in range(0, len(table), 16):
                name, address, size, info, other, index = struct.unpack_from("<IIIBBH", table, offset)
                name = self.string(names, name)
                self.symbols.setdefault(name, []).append((address, size, info, other, index))

    def extent(self, offset, size):
        if not 0 <= offset <= len(self.data) or not 0 <= size <= len(self.data) - offset:
            raise ValueError("truncated ELF extent")
        return self.data[offset:offset + size]

    @staticmethod
    def string(data, offset):
        if not 0 <= offset < len(data) or data.find(b"\0", offset) < 0:
            raise ValueError("invalid ELF string")
        return data[offset:data.index(b"\0", offset)].decode("ascii")

    def section_bytes(self, section):
        if section[1] == 8:
            raise ValueError("NOBITS has no file contents")
        return self.extent(section[4], section[5])

    def symbol(self, name):
        matches = self.symbols.get(name, [])
        if len(matches) != 1:
            raise ValueError("expected one ELF symbol: " + name)
        return matches[0]

    def object(self, name, size, kind, flags):
        address, actual_size, info, other, index = self.symbol(name)
        if (actual_size != size or info != 0x11 or other or not 0 < index < len(self.sections)):
            raise ValueError("invalid global ELF object: " + name)
        section = self.sections[index]
        if (section[1] != kind or section[2] != flags or address % 4
                or not section[3] <= address <= section[3] + section[5] - size):
            raise ValueError("ELF object outside expected section: " + name)
        return address, section


def resident_contract(data):
    """Verify actual readonly API bytes, owned BSS, heap boundary and stack extents."""
    legacy, flash = resident_elf(data)
    elf = Elf(data)
    address, section = elf.object("firmingo_sketch_api_v2", 52, 1, 2)
    if not 0x10000000 <= address <= 0x10200000 - 52:
        raise ValueError("candidate API outside resident flash")
    table = elf.extent(section[4] + address - section[3], 52)
    magic, version, size, *words = struct.unpack("<IHH11I", table)
    if (magic, version, size) != (0x49504146, 2, 52) or words[:8] != legacy["functionAddresses"]:
        raise ValueError("candidate API version, layout or calls differ")
    state, capacity, maximum = words[8:]
    allocation, _ = elf.object("firmingo_sketch_state", 4096, 8, 3)
    if (state != allocation or capacity != 4096 or maximum != 4096
            or not 0x20000000 <= state < state + capacity <= 0x20040000):
        raise ValueError("candidate API does not describe its actual 4 KiB BSS allocation")
    mappings = [payload[address - base:address - base + 52] for base, payload in flash
                if base <= address and address + 52 <= base + len(payload)]
    if mappings != [table]:
        raise ValueError("candidate API differs from flash LOAD bytes")
    # Other object symbols cannot own any part of the arena. Reject aliases too.
    for name, symbols in elf.symbols.items():
        for start, count, info, _, index in symbols:
            if (name != "firmingo_sketch_state" and count and info & 15 == 1
                    and 0 < index < len(elf.sections) and elf.sections[index][2] & 2
                    and start < state + capacity and state < start + count):
                raise ValueError("resident object overlaps state arena: " + name)
    names = ("__bss_start__", "__bss_end__", "__end__", "__HeapLimit",
             "__StackOneBottom", "__StackOneTop", "__StackBottom", "__StackTop")
    bounds = {name: elf.symbol(name)[0] for name in names}
    if not (bounds["__bss_start__"] <= state < state + capacity <= bounds["__bss_end__"]
            <= bounds["__end__"] < bounds["__HeapLimit"] == 0x20040000
            <= bounds["__StackOneBottom"] < bounds["__StackOneTop"]
            <= bounds["__StackBottom"] < bounds["__StackTop"] <= 0x20042000):
        raise ValueError("candidate arena/heap/stack linker extents overlap")
    return {"schema": 1, "purpose": "native stateful allocation/link proof only",
            "board": "nano_rp2040_connect", "imageVersion": 2, "residentABI": 2,
            "apiAddress": address, "apiTableBytes": 52, "apiTableSHA256": sha(table),
            "stateAddress": state, "stateCapacityBytes": capacity, "maximumCapsuleBytes": maximum,
            "residentELFSHA256": sha(data), "linkerBounds": bounds,
            "nominalHeapSpanBytes": bounds["__HeapLimit"] - bounds["__end__"],
            "runtimeEnabled": False, "hardwareTested": False}, flash


def module_capsule(data, contract):
    """Check the linked ELF extents and LOAD contents before emitting candidate FMS2."""
    elf = Elf(data)
    allowed = {".text": (1, 6), ".data": (1, 3), ".bss": (8, 3)}
    for name, section in elf.named.items():
        if section[2] & 2 and section[5] and (name not in allowed or (section[1], section[2]) != allowed[name]):
            raise ValueError("unsupported allocated module section: " + name)
        if section[1] in (4, 9) and section[5]:
            raise ValueError("module has unresolved relocations")
    for symbols in elf.symbols.values():
        if any(info >> 4 and index == 0 for _, _, info, _, index in symbols):
            raise ValueError("module has an undefined symbol")
    text, data_section, bss = [elf.named[name] for name in (".text", ".data", ".bss")]
    code, initialized = elf.section_bytes(text), elf.section_bytes(data_section)
    base = contract["stateAddress"]
    if (text[3] != CODE_ADDRESS or not code or len(code) % 4
            or data_section[3] != base or bss[3] != base + len(initialized)
            or len(initialized) % 4 or bss[5] % 4
            or len(initialized) + bss[5] > contract["stateCapacityBytes"]
            or 256 + len(code) + len(initialized) > contract["maximumCapsuleBytes"]):
        raise ValueError("module flash/RAM extent mismatch")
    expected_loads = []
    if code:
        expected_loads.append((CODE_ADDRESS, CODE_ADDRESS, code))
    if initialized:
        expected_loads.append((base, CODE_ADDRESS + len(code), initialized))
    loads = [(s[2], s[3], elf.extent(s[1], s[4])) for s in elf.segments if s[0] == 1 and s[4]]
    if loads != expected_loads:
        raise ValueError("module LOAD bytes do not match code and initialized-data extents")
    entries = []
    for name in ("firmingo_sketch_setup", "firmingo_sketch_loop"):
        address, _, info, _, index = elf.symbol(name)
        address |= 1
        if (info != 0x12 or not 0 < index < len(elf.sections)
                or elf.sections[index] != text
                or not CODE_ADDRESS <= address & ~1 <= CODE_ADDRESS + len(code) - 2):
            raise ValueError("module entry outside executable code")
        entries.append(address)
    header = bytearray(256)
    struct.pack_into("<4sHH14I", header, 0, b"FMS2", 2, 128, 0x4f4e414e,
                     contract["apiAddress"], CODE_ADDRESS, len(code), *entries,
                     base, len(initialized), base + len(initialized), bss[5],
                     256 + len(code), 256 + len(code) + len(initialized), 0, 0)
    payload = code + initialized
    header[64:96] = bytes.fromhex(sha(payload))
    header[96:128] = bytes.fromhex(sha(header[:96]))
    return bytes(header) + payload, {"codeBytes": len(code), "dataBytes": len(initialized),
                                    "bssBytes": bss[5], "setupAddress": entries[0], "loopAddress": entries[1]}


def run(command):
    print("+", " ".join(map(str, command)), flush=True)
    subprocess.run([str(p) for p in command], check=True, cwd=ROOT, timeout=660)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", required=True, type=Path, help="Arduino CLI 1.5.1 executable")
    parser.add_argument("--config-file", required=True, type=Path, help="pinned patched overlay config")
    parser.add_argument("--validator", required=True, type=Path, help="CMake managed_stateful_check executable")
    parser.add_argument("--output-dir", required=True, type=Path, help="new output directory; refuses existing paths")
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {"status": "failed", "runtimeEnabled": False, "hardwareTested": False}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    sources = [HERE / name for name in (
        "build_stateful_proof.py", "build_resident.py", "stateful_abi.h", "abi.h", "facade/Arduino.h",
        "stateful_counter.ino", "module_entry.cpp", "stateful_module.ld", "stateful_check.cpp",
        "stateful_image.h", "stateful_image.cpp", "image.h", "image.cpp", "export_resident.py", "build_composite.py")]
    try:
        report["sourceSHA256"] = {str(p.relative_to(ROOT)): sha(p.read_bytes()) for p in sources}
        config = args.config_file.resolve()
        tool_bin = config.parent / "data/packages/rp2040/tools/pqt-gcc/5.0.0-9576866/bin"
        compiler = tool_bin / "arm-none-eabi-g++"
        if not compiler.is_file() or not args.validator.is_file():
            raise ValueError("pinned ARM compiler or native validator missing")
        run([sys.executable, HERE / "build_resident.py", "--cli", args.cli.resolve(),
             "--config-file", config, "--output-dir", output / "resident", "--stateful-allocation-proof"])
        resident = output / "resident"
        resident_report = json.loads(bounded_file(resident / "report.json", 262144), object_pairs_hook=unique_object)
        if (resident_report.get("status") != "compile-tested stateful allocation candidate; FMS2 runtime disabled; not flashed"
                or resident_report.get("stateful_runtime_enabled") is not False or "upload_target" in resident_report):
            raise ValueError("select explicit candidate allocation build")
        elf = bounded_file(resident / "artifacts/firmingo_managed_resident.ino.elf", ELF_LIMIT)
        uf2 = bounded_file(resident / "artifacts/firmingo_managed_resident.ino.uf2", UF2_LIMIT)
        if sha(elf) != resident_report["elf_sha256"] or sha(uf2) != resident_report["uf2_sha256"]:
            raise ValueError("resident report/artifacts mismatch")
        contract, flash = resident_contract(elf)
        contract["residentUF2SHA256"] = sha(uf2)
        contract["elfUF2ComparedBytes"] = check_uf2(uf2, flash)
        (output / "linker-contract.json").write_text(json.dumps(contract, indent=2) + "\n")
        objects = []
        for source in (HERE / "stateful_counter.ino", HERE / "module_entry.cpp"):
            obj = output / (source.stem + ".o")
            run([compiler, "-mcpu=cortex-m0plus", "-mthumb", "-Os", "-std=c++11", "-ffreestanding",
                 "-fno-builtin", "-fno-exceptions", "-fno-rtti", "-fno-unwind-tables",
                 "-fno-asynchronous-unwind-tables", "-fno-stack-protector", "-fno-common",
                 "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra", "-Werror",
                 "-I" + str(HERE / "facade"), f"-DFIRMINGO_API_ADDRESS={contract['apiAddress']}",
                 "-x", "c++", "-c", source, "-o", obj])
            objects.append(obj)
        module = output / "stateful_counter.elf"
        run([compiler, "-mcpu=cortex-m0plus", "-mthumb", "-nostdlib", "-Wl,--gc-sections",
             "-Wl,--no-undefined", "-Wl,--build-id=none", "-Wl,-e,firmingo_sketch_setup",
             f"-Wl,--defsym,FIRMINGO_STATE_ADDRESS={contract['stateAddress']}",
             f"-Wl,--defsym,FIRMINGO_STATE_BYTES={contract['stateCapacityBytes']}",
             "-Wl,-T," + str(HERE / "stateful_module.ld"), "-Wl,-Map," + str(output / "stateful_counter.map"),
             *objects, "-o", module])
        module_bytes = bounded_file(module, ELF_LIMIT)
        capsule, extents = module_capsule(module_bytes, contract)
        # This selected proof must actually exercise both mutable section kinds.
        if extents["dataBytes"] != 4 or extents["bssBytes"] != 4 or capsule[-4:] != struct.pack("<I", 7):
            raise ValueError("counter proof must contain its initialized word and BSS word")
        capsule_path = output / "stateful_counter.fms"
        capsule_path.write_bytes(capsule)
        command = [args.validator.resolve(), capsule_path, contract["apiAddress"],
                   contract["stateAddress"], contract["stateCapacityBytes"]]
        run(command)
        # Prove the same C++ checker rejects a corrupted real compiler output.
        corrupt = output / "corrupt.fms"
        corrupt.write_bytes(capsule[:-1] + bytes([capsule[-1] ^ 1]))
        rejected = subprocess.run([str(p) for p in [*command[:1], corrupt, *command[2:]]],
                                  capture_output=True, text=True, timeout=10)
        corrupt.unlink()
        if rejected.returncode != 1 or "capsule rejected" not in rejected.stderr:
            raise ValueError("production validator did not reject corrupt generated capsule")
        for name, expected in {**resident_report["source_sha256"], **report["sourceSHA256"]}.items():
            if sha((ROOT / name).read_bytes()) != expected:
                raise ValueError("proof source changed during build: " + name)
        report.update(status="native ARM link and host initialization passed; not executed on ARM",
                      contract=contract, sections=extents, capsuleBytes=len(capsule), capsuleSHA256=sha(capsule),
                      moduleELFSHA256=sha(module_bytes), validatorSHA256=sha(args.validator.read_bytes()),
                      corruptCapsuleRejected=True, flashBytes=resident_report["flash_bytes"],
                      staticRAMBytes=resident_report["static_ram_bytes"],
                      residentReportSHA256=sha((resident / "report.json").read_bytes()),
                      compilerSHA256=sha(compiler.read_bytes()),
                      compilerVersion=subprocess.run([str(compiler), "--version"], capture_output=True,
                                                     text=True, check=True, timeout=10).stdout.splitlines()[0])
    except Exception as error:
        report["error"] = str(error)
        raise
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
