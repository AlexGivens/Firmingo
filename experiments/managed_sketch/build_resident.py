#!/usr/bin/env python3
"""Compile the experimental Nano resident using a pinned, patched core overlay."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked_ram_park(config, elf):
    tool_bin = (config.parent / "data/packages/rp2040/tools/pqt-gcc"
                / "5.0.0-9576866/bin")
    nm = tool_bin / "arm-none-eabi-nm"
    objdump = tool_bin / "arm-none-eabi-objdump"
    if not nm.is_file() or not objdump.is_file():
        raise RuntimeError("pinned ARM symbol/disassembly tools are missing")
    symbols = subprocess.run([str(nm), "-C", "-S", str(elf)], capture_output=True,
                             text=True, check=True, timeout=30).stdout
    matches = re.findall(
        r"^([0-9a-fA-F]+) ([0-9a-fA-F]+) T "
        r"firmingo_managed::Core1Pause::park_if_requested\(\)$",
        symbols, re.MULTILINE)
    if len(matches) != 1:
        raise RuntimeError("expected one core-1 park function in resident ELF")
    address, size = (int(value, 16) for value in matches[0])
    if not size or not 0x20000000 <= address < address + size <= 0x20042000:
        raise RuntimeError("core-1 park function is not wholly in RP2040 SRAM")
    disassembly = subprocess.run(
        [str(objdump), "-d", "-C", f"--start-address={address}",
         f"--stop-address={address + size}", str(elf)],
        capture_output=True, text=True, check=True, timeout=30).stdout
    if "<firmingo_managed::Core1Pause::park_if_requested()>:" not in disassembly:
        raise RuntimeError("core-1 park disassembly is missing")
    if re.search(r"\bblx?\b|\bldr\s+[^\n]*\[pc", disassembly):
        raise RuntimeError("core-1 park function calls a helper or loads from PC")
    return address, size


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", required=True, type=Path,
                        help="Arduino CLI 1.5.1 executable")
    parser.add_argument("--config-file", required=True, type=Path,
                        help="Arduino CLI config for a patched Arduino-Pico 6.0.0 overlay")
    parser.add_argument("--output-dir", type=Path,
                        default=ROOT / "build/managed_sketch/resident")
    args = parser.parse_args()
    cli, config = args.cli.resolve(), args.config_file.resolve()
    board = json.loads((ROOT / "boards/nano_rp2040_connect/baseline.json").read_text())
    version = subprocess.run([str(cli), "--config-file", str(config),
                              "version", "--json"], capture_output=True,
                             text=True, check=True, timeout=30)
    if json.loads(version.stdout).get("VersionString") != "1.5.1":
        parser.error("Arduino CLI 1.5.1 is required")
    cores = subprocess.run([str(cli), "--config-file", str(config),
                            "core", "list", "--json"], capture_output=True,
                           text=True, check=True, timeout=30)
    installed = {item["id"]: item.get("installed_version")
                 for item in json.loads(cores.stdout).get("platforms", [])}
    if installed.get("rp2040:rp2040") != "6.0.0":
        parser.error("the selected config must contain Arduino-Pico 6.0.0")
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    sketch = HERE / "firmingo_managed_resident"
    command = [
        str(cli), "--config-file", str(config), "compile",
        "--fqbn", board["fqbn"], "--warnings", "all",
        "--build-path", str(output / "work"),
        "--output-dir", str(output / "artifacts"),
        str(sketch), "--library", str(ROOT), "--build-property",
        "compiler.cpp.extra_flags=-I" + str(HERE) + " -DFIRMINGO_NET_C=77 "
        "-DFIRMINGO_USB_DEFERRED_START -DDISABLE_USB_SERIAL",
    ]
    print("+", " ".join(command), flush=True)
    with (output / "build.log").open("w") as log:
        result = subprocess.run(command, cwd=ROOT, stdout=log,
                                stderr=subprocess.STDOUT, timeout=600)
    log_text = (output / "build.log").read_text()
    if result.returncode:
        print(log_text[-8000:])
        raise SystemExit(f"resident compile failed; see {output / 'build.log'}")
    flash = re.search(r"Sketch uses (\d+) bytes", log_text)
    ram = re.search(r"Global variables use (\d+) bytes", log_text)
    if not flash or not ram:
        raise SystemExit("resident size summary missing")
    if int(flash[1]) > 1024 * 1024 or int(ram[1]) > 192 * 1024:
        raise SystemExit("resident exceeds the 1 MiB flash / 192 KiB static-RAM engineering budget")
    elf = output / "artifacts/firmingo_managed_resident.ino.elf"
    uf2 = output / "artifacts/firmingo_managed_resident.ino.uf2"
    if not elf.is_file() or not uf2.is_file():
        raise SystemExit("resident ELF or UF2 missing")
    park_address, park_bytes = checked_ram_park(config, elf)
    report = {
        "status": "compile-tested experimental resident; not flashed",
        "board": board["board"],
        "core": "rp2040:rp2040@6.0.0",
        "fqbn": board["fqbn"],
        "flash_bytes": int(flash[1]),
        "static_ram_bytes": int(ram[1]),
        "flash_budget_bytes": 1024 * 1024,
        "static_ram_budget_bytes": 192 * 1024,
        "upload_target": {"id": 1, "format": "nano-managed-v1", "max_size": 4096,
                          "max_chunk": 1024, "authorization": "physical-d2-gnd"},
        "elf_sha256": sha(elf),
        "uf2_sha256": sha(uf2),
        "core1_park_ram_address": f"0x{park_address:08x}",
        "core1_park_bytes": park_bytes,
        "source_sha256": {
            str(p.relative_to(ROOT)): sha(p)
            for p in sorted([
                sketch / "firmingo_managed_resident.ino",
                HERE / "build_resident.py",
                HERE / "abi.h", HERE / "image.h", HERE / "image.cpp",
                HERE / "mailbox.h", HERE / "console.h",
                HERE / "core1_pause.h", HERE / "core1_pause.cpp",
                HERE / "stage.h", HERE / "stage.cpp",
                HERE / "install.h", HERE / "install.cpp",
                HERE / "switch.h", HERE / "switch.cpp",
                HERE / "fmgo_upload.h", HERE / "fmgo_upload.cpp",
                HERE / "upload_wire.h", HERE / "upload_wire.cpp",
                HERE / "upload_server.h", HERE / "upload_server.cpp",
                ROOT / "boards/nano_rp2040_connect/baseline.json",
                *[p for p in (ROOT / "src").rglob("*") if p.is_file()],
            ])
        },
    }
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({key: value for key, value in report.items()
                      if key != "source_sha256"}, indent=2))


if __name__ == "__main__":
    main()
