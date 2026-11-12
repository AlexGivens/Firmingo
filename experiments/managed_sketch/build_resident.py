#!/usr/bin/env python3
"""Compile the experimental Nano resident using a pinned, patched core overlay."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
from tools.version import fms2_candidate_version

FMS2_UPLOAD_STATUS = "compile-tested FMS2 upload candidate; not flashed"
FMS2_RELEASE_STATUS = "compile-tested FMS2 release candidate; not flashed"


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


def source_inventory(sketch, args, stateful_runtime):
    names = [
        "build_resident.py", "abi.h", "image.h", "image.cpp", "mailbox.h", "console.h",
        "core1_pause.h", "core1_pause.cpp", "stage.h", "stage.cpp", "install.h", "install.cpp",
        "switch.h", "switch.cpp", "fmgo_upload.h", "fmgo_upload.cpp", "upload_wire.h",
        "upload_wire.cpp", "upload_server.h", "upload_server.cpp",
    ]
    if args.stateful_allocation_proof or stateful_runtime:
        names.append("stateful_abi.h")
    if stateful_runtime:
        names.extend(("stateful_image.h", "stateful_image.cpp", "stateful_runtime.h", "stateful_runtime.cpp"))
    if args.stateful_upload_proof:
        names.extend(("stateful_upload.h", "stateful_upload.cpp"))
    paths = [sketch / "firmingo_managed_resident.ino", ROOT / "boards/nano_rp2040_connect/baseline.json",
             *(HERE / name for name in names), *(p for p in (ROOT / "src").rglob("*") if p.is_file())]
    if args.stateful_upload_proof:
        paths.extend(ROOT / name for name in (
            "boards/nano_rp2040_connect/core-lock.json",
            "patches/arduino-pico-6.0.0-usb-startup/manifest.json",
            "patches/arduino-pico-6.0.0-usb-startup/deferred-ncm.patch"))
    if args.fms2_release_candidate:
        paths.extend((ROOT / "library.properties", ROOT / "tools/version.py"))
    return {path.relative_to(ROOT).as_posix(): sha(path) for path in sorted(paths)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", required=True, type=Path,
                        help="Arduino CLI 1.5.1 executable")
    parser.add_argument("--config-file", required=True, type=Path,
                        help="Arduino CLI config for a patched Arduino-Pico 6.0.0 overlay")
    parser.add_argument("--output-dir", type=Path,
                        default=ROOT / "build/managed_sketch/resident")
    parser.add_argument("--stateful-allocation-proof", action="store_true",
                        help="opt-in candidate FAPI two and 4 KiB .bss arena; FMS2 execution remains disabled")
    parser.add_argument("--stateful-runtime-proof", action="store_true",
                        help="opt-in FMS2 boot runtime with owned arena; all network uploads disabled")
    parser.add_argument("--stateful-upload-proof", action="store_true",
                        help="opt-in FMS2 boot and physically armed target-2 network uploads")
    parser.add_argument("--fms2-release-candidate", action="store_true",
                        help="build the normal FMS2 upload runtime with its release-candidate identity")
    parser.add_argument("--flash-checkpoint", choices=("erase", "first-page"),
                        help="destructive test only: hold after a completed command, then fail; requires --stateful-upload-proof")
    args = parser.parse_args()
    candidate_version = None
    if args.fms2_release_candidate:
        if args.stateful_allocation_proof or args.stateful_runtime_proof or args.stateful_upload_proof or args.flash_checkpoint:
            parser.error("select the FMS2 release candidate without experimental or fault modes")
        candidate_version = fms2_candidate_version((ROOT / "src/core/version.h").read_bytes())
        args.stateful_upload_proof = True
    if args.flash_checkpoint and not args.stateful_upload_proof:
        parser.error("--flash-checkpoint requires --stateful-upload-proof")
    stateful_runtime = args.stateful_runtime_proof or args.stateful_upload_proof
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
    # A failed rebuild must never leave an older successful report behind.
    (output / "report.json").unlink(missing_ok=True)
    sketch = HERE / "firmingo_managed_resident"
    sources = source_inventory(sketch, args, stateful_runtime)
    command = [
        str(cli), "--config-file", str(config), "compile",
        "--fqbn", board["fqbn"], "--warnings", "all",
        "--build-path", str(output / "work"),
        "--output-dir", str(output / "artifacts"),
        str(sketch), "--library", str(ROOT), "--build-property",
        "compiler.cpp.extra_flags=-I" + str(HERE) + " -DFIRMINGO_NET_C=77 "
        "-DFIRMINGO_USB_DEFERRED_START -DDISABLE_USB_SERIAL",
    ]
    if args.stateful_allocation_proof or stateful_runtime:
        command[-1] += " -DFIRMINGO_STATEFUL_ALLOCATION_PROOF"
        command += ["--build-property",
                    "compiler.c.elf.extra_flags=-Wl,-u,firmingo_sketch_api_v2"]
        if stateful_runtime:
            command[-3] += " -DFIRMINGO_STATEFUL_RUNTIME_PROOF"
            command[-1] += " -Wl,-u,firmingo_sketch_api -Wl,-u,firmingo_stateful_boot_mode"
            if args.stateful_upload_proof:
                command[-3] += " -DFIRMINGO_STATEFUL_UPLOAD_PROOF"
                command[-1] += " -Wl,-u,firmingo_stateful_upload_contract"
                if candidate_version is not None:
                    command[-3] += " -DFIRMINGO_FMS2_RELEASE_CANDIDATE"
                if args.flash_checkpoint:
                    checkpoint = 1 if args.flash_checkpoint == "erase" else 2
                    command[-3] += f" -DFIRMINGO_FLASH_CHECKPOINT={checkpoint}"
                    command[-1] += " -Wl,-u,firmingo_flash_checkpoint_contract"
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
    if source_inventory(sketch, args, stateful_runtime) != sources:
        raise SystemExit("resident sources changed during compilation")
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
        "source_sha256": sources,
    }
    if args.stateful_allocation_proof or stateful_runtime:
        report["status"] = "compile-tested stateful allocation candidate; FMS2 runtime disabled; not flashed"
        report["legacy_upload_target"] = report.pop("upload_target")
        report["stateful_runtime_enabled"] = False
    if stateful_runtime:
        report["status"] = "compile-tested FMS2 boot runtime candidate; network uploads disabled; not flashed"
        report.pop("legacy_upload_target")
        report["stateful_runtime_enabled"] = True
        report["network_uploads_enabled"] = False
    if args.stateful_upload_proof:
        report["status"] = FMS2_UPLOAD_STATUS
        report["network_uploads_enabled"] = True
        report["upload_target"] = {"id": 2, "format": "nano-managed-v2", "max_size": 4096,
                                   "max_chunk": 1024, "authorization": "physical-d2-gnd"}
    if candidate_version is not None:
        report["status"] = FMS2_RELEASE_STATUS
        report["firmware_version"] = candidate_version
    if args.flash_checkpoint:
        report["status"] = "compile-tested FMS2 flash-checkpoint fault candidate; not flashed"
        report["flash_checkpoint"] = dict(after=args.flash_checkpoint, hold_ms=60000,
                                         expiry="fail without continuing", chip_busy_interruption=False)
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({key: value for key, value in report.items()
                      if key != "source_sha256"}, indent=2))


if __name__ == "__main__":
    main()
