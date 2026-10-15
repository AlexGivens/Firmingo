"""First-class Nano managed build using the existing resident and independent validators.

This is a release-engineering foundation, not promotion of the experimental ABI.
The separate profile report records failure before any toolchain or build work.
"""

import json
from pathlib import Path
import shutil
import sys

from experiments.managed_sketch.export_resident import (
    ELF_LIMIT, UF2_LIMIT, bounded_file, check_uf2, resident_elf, sha, unique_object,
)


def verify_outputs(root, resident, manifest):
    """Check actual ELF/UF2, source identities and budgets without assuming build success."""
    report_bytes = bounded_file(resident / "report.json", 256 * 1024)
    report = json.loads(report_bytes, object_pairs_hook=unique_object)
    if (not isinstance(report, dict)
            or report.get("status") != "compile-tested experimental resident; not flashed"
            or report.get("board") != manifest["board"]
            or report.get("core") != "rp2040:rp2040@6.0.0"
            or report.get("fqbn") != manifest["fqbn"]
            or report.get("upload_target") != {
                "id": 1, "format": "nano-managed-v1", "max_size": 4096,
                "max_chunk": 1024, "authorization": "physical-d2-gnd"}):
        raise ValueError("managed resident report does not match the selected experimental profile")
    for field in ("flash_bytes", "static_ram_bytes"):
        budget = field.replace("bytes", "budget_bytes")
        if (type(report.get(field)) is not int
                or not 0 < report[field] <= manifest[budget]
                or report.get(budget) != manifest[budget]):
            raise ValueError("managed resident memory budget mismatch")
    sources = report.get("source_sha256")
    required = {
        "boards/nano_rp2040_connect/baseline.json", "src/core/version.h",
        "experiments/managed_sketch/abi.h", "experiments/managed_sketch/build_resident.py",
        "experiments/managed_sketch/firmingo_managed_resident/firmingo_managed_resident.ino",
    }
    required.update(path.relative_to(root).as_posix() for path in (root / "src").rglob("*")
                    if path.is_file() and path.suffix in (".c", ".cpp", ".h", ".hpp", ".inc", ".S", ".s"))
    if not isinstance(sources, dict) or not required <= sources.keys() or len(sources) > 256:
        raise ValueError("managed resident source inventory is incomplete")
    for name, expected in sources.items():
        path = Path(name)
        if (not isinstance(name, str) or path.is_absolute()
                or any(part in ("", ".", "..") for part in name.split("/"))):
            raise ValueError("unsafe managed resident source path")
        if sha(bounded_file(root / path, 2 * 1024 * 1024)) != expected:
            raise ValueError("managed resident source changed during build: " + name)
    artifacts = {}
    for extension, limit in (("elf", ELF_LIMIT), ("uf2", UF2_LIMIT)):
        name = "firmingo_managed_resident.ino." + extension
        data = bounded_file(resident / "artifacts" / name, limit)
        if sha(data) != report.get(extension + "_sha256"):
            raise ValueError("managed resident artifact hash mismatch: " + extension)
        artifacts[extension] = data
    api, flash = resident_elf(artifacts["elf"])
    compared = check_uf2(artifacts["uf2"], flash)
    return {
        "resident_report_sha256": sha(report_bytes),
        "resident_elf_sha256": sha(artifacts["elf"]),
        "resident_uf2_sha256": sha(artifacts["uf2"]),
        "api_address": f"0x{api['apiAddress']:08x}",
        "resident_abi": api["residentABI"], "elf_uf2_compared_bytes": compared,
        "flash_bytes": report["flash_bytes"], "static_ram_bytes": report["static_ram_bytes"],
    }


def build_managed(args, manifest):
    """Prepare the pinned overlay, build once and validate without contacting hardware."""
    from tools import dev
    from tools.usb_core import prepare

    if args.board != "nano_rp2040_connect":
        raise ValueError("managed firmware is available only for nano_rp2040_connect")
    if args.subnet_octet != 77 or args.verbose:
        raise ValueError("managed currently fixes subnet 77 and uses resident/build.log; omit subnet/verbose overrides")
    output = dev.ROOT / "build/firmware" / args.board / "managed"
    output.mkdir(parents=True, exist_ok=True)
    profile = {"schema": 1, "firmware": "managed", "board": args.board,
               "status": "failed", "qualification": "experimental",
               "production_ready": False, "hardware_tested": False,
               "resident_report": "resident/report.json"}
    receipt = output / "profile-report.json"
    receipt.write_text(json.dumps(profile, indent=2) + "\n")
    # Invalidate an earlier resident success even if setup fails before compilation.
    resident = output / "resident"
    if (resident / "report.json").exists():
        (resident / "report.json").unlink()
    try:
        profile["source_sha256"] = {name: dev.digest(dev.ROOT / name) for name in (
            "tools/dev.py", "tools/managed_profile.py", "tools/usb_core.py",
            "boards/nano_rp2040_connect/baseline.json",
            "boards/nano_rp2040_connect/core-lock.json",
            "patches/arduino-pico-6.0.0-usb-startup/manifest.json")}
        cli = dev.check_arduino(manifest)
        config = dev.effective_arduino_config(cli)
        cli, overlay = prepare(dev.ROOT, cli, config, output / "usb-toolchain")
        profile["usb_startup_overlay"] = overlay
        # Retain intermediate caches/logs, but never reuse an older exported image.
        if (resident / "artifacts").exists():
            shutil.rmtree(resident / "artifacts")
        command = [sys.executable, dev.ROOT / "experiments/managed_sketch/build_resident.py",
                   "--cli", cli[0], "--config-file", cli[2], "--output-dir", resident]
        dev.run(command, timeout=660)
        profile.update(verify_outputs(dev.ROOT, resident, manifest))
        for name, expected in profile["source_sha256"].items():
            if dev.digest(dev.ROOT / name) != expected:
                raise ValueError("managed profile input changed during build: " + name)
        profile["status"] = "compile-tested"
    except Exception as error:
        profile["error"] = str(error)
        raise
    finally:
        receipt.write_text(json.dumps(profile, indent=2) + "\n")
    print(f"Managed profile report: {receipt}; experimental ABI one, hardware NOT tested")
    return 0
