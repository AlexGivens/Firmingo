#!/usr/bin/env python3
"""Install one exact experimental managed-sketch UF2 on a selected Nano."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def install_with_picotool(picotool, flash_id, image):
    """Load only after ROM identity matches; verify all UF2 blocks before reboot."""
    tool = picotool.resolve(strict=True)
    if (tool.name != "picotool" or tool.parent.name != "5.0.0-9576866"
            or tool.parent.parent.name != "pqt-picotool"):
        raise ValueError("--picotool must select pinned pqt-picotool/5.0.0-9576866")
    info = subprocess.run([str(tool), "info", "-a"], check=True,
                          capture_output=True, text=True, timeout=30).stdout
    observed = re.findall(r"^\s*flash id:\s*0x([0-9a-fA-F]{16})\s*$",
                          info, re.MULTILINE)
    if len(observed) != 1 or observed[0].lower() != flash_id:
        raise RuntimeError("ROM flash ID does not match selected Nano")
    subprocess.run([str(tool), "load", "-v", str(image), "-t", "uf2"],
                   check=True, timeout=120)
    subprocess.run([str(tool), "verify", str(image), "-t", "uf2"],
                   check=True, timeout=120)
    subprocess.run([str(tool), "reboot"], check=True, timeout=30)
    print("Full UF2 verified before reboot; boot and network behavior are unverified.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", choices=("nano_rp2040_connect",), required=True,
                        help="operator-selected physical board; ROM reports only generic RPI-RP2")
    parser.add_argument("--sketch", choices=("blink_a", "blink_b"), required=True)
    transport = parser.add_mutually_exclusive_group(required=True)
    transport.add_argument("--mount", type=Path,
                           help="copy to mounted RPI-RP2 volume; flash readback is not verified")
    transport.add_argument("--picotool", type=Path,
                           help="pinned picotool for ROM load, full verify, and reboot")
    parser.add_argument("--expected-flash-id",
                        help="16-digit ROM flash ID of the selected Nano; required with --picotool")
    parser.add_argument("--expected-uf2-sha256", required=True,
                        help="exact composite SHA-256 from the reviewed build report")
    parser.add_argument("--resident-dir", type=Path,
                        default=ROOT / "build/managed_sketch/resident")
    parser.add_argument("--module-dir", type=Path,
                        default=ROOT / "build/managed_sketch")
    parser.add_argument("--output-dir", type=Path,
                        default=ROOT / "build/managed_sketch/composite")
    args = parser.parse_args()
    expected = args.expected_uf2_sha256.lower()
    if not re.fullmatch(r"[0-9a-f]{64}", expected):
        parser.error("--expected-uf2-sha256 must be 64 hexadecimal digits")
    flash_id = (args.expected_flash_id or "").lower()
    if args.picotool and not re.fullmatch(r"[0-9a-f]{16}", flash_id):
        parser.error("--picotool requires --expected-flash-id with 16 hexadecimal digits")
    if args.mount and args.expected_flash_id:
        parser.error("--expected-flash-id applies only with --picotool")
    if args.mount:
        mount = args.mount.resolve(strict=True)
        if not mount.is_dir() or not os.path.ismount(mount):
            parser.error("--mount must select an existing mounted volume")
        info = (mount / "INFO_UF2.TXT").read_text()
        if "Board-ID: RPI-RP2" not in info:
            parser.error("selected volume does not identify an RP2040 ROM bootloader")

    # Regeneration checks both source snapshots, the exact resident ELF/UF2,
    # the module capsule, image header, and every UF2 address bound.
    subprocess.run([sys.executable, str(HERE / "build_composite.py"),
                    "--sketch", args.sketch,
                    "--resident-dir", str(args.resident_dir),
                    "--module-dir", str(args.module_dir),
                    "--output-dir", str(args.output_dir)],
                   cwd=ROOT, check=True, timeout=30)
    output = args.output_dir.resolve()
    report = json.loads((output / f"{args.sketch}-report.json").read_text())
    image = output / f"{args.sketch}-initial-rom-only.uf2"
    if (report.get("status") != "ROM-only initial proof image; not flashed or hardware-verified"
            or report.get("board") != "nano_rp2040_connect"
            or report.get("sketch") != args.sketch
            or report.get("composite_uf2_sha256") != expected
            or digest(image) != expected):
        raise RuntimeError("reviewed SHA-256 does not match the current composite UF2")
    if args.picotool:
        print(f"Loading and verifying {args.sketch} image {expected} on ROM flash ID {flash_id}",
              flush=True)
        install_with_picotool(args.picotool, flash_id, image)
    else:
        destination = mount / "FIRMINGO-M7.UF2"
        print(f"Copying {args.sketch} experimental image {expected} to {mount}", flush=True)
        with image.open("rb") as source, destination.open("wb") as target:
            shutil.copyfileobj(source, target)
            target.flush()
            os.fsync(target.fileno())
        print("UF2 copied without flash readback; boot and sketch execution are unverified.")


if __name__ == "__main__":
    main()
