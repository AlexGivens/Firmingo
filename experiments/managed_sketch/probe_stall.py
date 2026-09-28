#!/usr/bin/env python3
"""Opt-in Nano fault: stall core 1, check resident timeout, require ROM recovery."""

import argparse
import ipaddress
import json
import os
from pathlib import Path
import re
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from experiments.managed_sketch import upload as managed_upload
from experiments.managed_sketch.probe_pause import sketch_control
from tools.session_smoke import Probe
from tools.version import current_version


def checked_recovery_image(expected):
    """Require the exact, already proven Blink B ROM image before stalling."""
    output = ROOT / "build/managed_sketch/composite"
    report = json.loads((output / "blink_b-report.json").read_text())
    image = output / "blink_b-initial-rom-only.uf2"
    if (report.get("status") != "ROM-only initial proof image; not flashed or hardware-verified"
            or report.get("board") != "nano_rp2040_connect"
            or report.get("sketch") != "blink_b"
            or report.get("composite_uf2_sha256") != expected
            or managed_upload.sha(image.read_bytes()) != expected):
        raise RuntimeError("exact Blink B ROM recovery image is unavailable")
    return image


def checked_picotool(path):
    tool = path.resolve(strict=True)
    if (tool.name != "picotool" or tool.parent.name != "5.0.0-9576866"
            or tool.parent.parent.name != "pqt-picotool" or not os.access(tool, os.X_OK)):
        raise RuntimeError("pinned picotool for ROM recovery is unavailable")
    return tool


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", required=True)
    parser.add_argument("--device-id", required=True)
    parser.add_argument("--expected-resident-uf2-sha256", required=True)
    parser.add_argument("--expected-stall-capsule-sha256", required=True)
    parser.add_argument("--expected-blink-b-capsule-sha256", required=True)
    parser.add_argument("--expected-recovery-uf2-sha256", required=True)
    parser.add_argument("--picotool", required=True, type=Path,
                        help="pinned pqt-picotool/5.0.0-9576866 recovery tool")
    parser.add_argument("--run", action="store_true",
                        help="permanently stall the sketch until physical ROM recovery")
    args = parser.parse_args()
    address = str(ipaddress.IPv4Address(args.address))
    device_id = args.device_id.lower()
    if not re.fullmatch(r"[0-9a-f]{16}", device_id):
        parser.error("--device-id must be 16 hexadecimal digits")
    hash_names = ("expected_resident_uf2_sha256", "expected_stall_capsule_sha256",
                  "expected_blink_b_capsule_sha256", "expected_recovery_uf2_sha256")
    for name in hash_names:
        if not re.fullmatch(r"[0-9a-fA-F]{64}", getattr(args, name)):
            parser.error(f"--{name.replace('_', '-')} must be 64 hexadecimal digits")
    if not args.run:
        parser.error("--run is required; this test requires physical ROM recovery")

    resident_hash = args.expected_resident_uf2_sha256.lower()
    managed_upload.verified_capsule("stall_probe", resident_hash,
                                    args.expected_stall_capsule_sha256.lower())
    blink_b = managed_upload.verified_capsule(
        "blink_b", resident_hash, args.expected_blink_b_capsule_sha256.lower())
    recovery = checked_recovery_image(args.expected_recovery_uf2_sha256.lower())
    tool = checked_picotool(args.picotool)
    before = managed_upload.selected_hello(address, device_id)
    target = dict(address=address, device_id=device_id,
                  board="nano_rp2040_connect", timeout=10,
                  firmware_version=current_version() + "-m7exp3")
    with Probe(**target) as probe:
        probe.command("serial.open", channel_id=1)
        sketch_control(probe, 0xf6, 0xf7)
        sketch_control(probe, 0xf4, 0xf5)
        triggered_at = time.monotonic()
        print("Stall acknowledged; physical REC/GND ROM recovery is now required.",
              flush=True)
        probe.command("serial.close", channel_id=1)
        probe.finish()

    during = managed_upload.selected_hello(address, device_id)
    if during["boot_id"] != before["boot_id"]:
        raise RuntimeError("resident rebooted after the sketch stall; inspect in ROM mode")
    response = managed_upload.upload(address, device_id, blink_b, 10,
                                     expected_response=b"T")
    elapsed = time.monotonic() - triggered_at
    after = managed_upload.selected_hello(address, device_id)
    if after["boot_id"] != before["boot_id"]:
        raise RuntimeError("resident rebooted during stalled-sketch upload; inspect in ROM mode")
    print(json.dumps({"result": "stalled_sketch_refused_before_flash",
                      "upload_response": response.decode(), "seconds_to_refusal": elapsed,
                      "same_boot": True, "resident_hello_during_stall": True,
                      "recovery_uf2": str(recovery),
                      "recovery_picotool": str(tool),
                      "recovery_uf2_sha256": args.expected_recovery_uf2_sha256.lower()},
                     indent=2))
    print("Sketch is intentionally stuck. Use REC/GND ROM mode; inspect the "
          "slot, then restore the exact Blink B composite with flash_initial.py --picotool.")


if __name__ == "__main__":
    main()
