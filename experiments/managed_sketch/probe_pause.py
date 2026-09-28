#!/usr/bin/env python3
"""Opt-in Nano check: a busy sketch must time out before any slot erase."""

import argparse
import ipaddress
import json
from pathlib import Path
import re
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from experiments.managed_sketch import upload as managed_upload
from tools.session_smoke import HEADER, Probe, decode_header, frame
from tools.version import current_version


def sketch_control(probe, request, expected):
    """Exchange one diagnostic sketch byte through the production FMGO stream."""
    probe.sock.sendall(frame(3, 0, b"\0\0\0\1" + bytes((request,))))
    deadline = time.monotonic() + probe.timeout
    kind, request_id, size = decode_header(probe._exact(HEADER.size, deadline))
    if (kind, request_id, size) != (3, 0, 5):
        raise RuntimeError("unexpected sketch response frame")
    payload = probe._exact(size, deadline)
    if payload != b"\0\0\0\1" + bytes((expected,)):
        raise RuntimeError(f"unexpected sketch response byte: {payload.hex()}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", required=True)
    parser.add_argument("--device-id", required=True)
    parser.add_argument("--expected-resident-uf2-sha256", required=True)
    parser.add_argument("--expected-pause-capsule-sha256", required=True)
    parser.add_argument("--expected-blink-b-capsule-sha256", required=True)
    parser.add_argument("--run", action="store_true",
                        help="send a candidate upload while the diagnostic sketch is busy")
    args = parser.parse_args()
    address = str(ipaddress.IPv4Address(args.address))
    device_id = args.device_id.lower()
    if not re.fullmatch(r"[0-9a-f]{16}", device_id):
        parser.error("--device-id must be 16 hexadecimal digits")
    for name in ("expected_resident_uf2_sha256", "expected_pause_capsule_sha256",
                 "expected_blink_b_capsule_sha256"):
        if not re.fullmatch(r"[0-9a-fA-F]{64}", getattr(args, name)):
            parser.error(f"--{name.replace('_', '-')} must be 64 hexadecimal digits")
    if not args.run:
        parser.error("--run is required for the opt-in bounded-pause test")
    resident_hash = args.expected_resident_uf2_sha256.lower()
    managed_upload.verified_capsule("pause_probe", resident_hash,
                                    args.expected_pause_capsule_sha256.lower())
    blink_b = managed_upload.verified_capsule(
        "blink_b", resident_hash, args.expected_blink_b_capsule_sha256.lower())
    before = managed_upload.selected_hello(address, device_id)
    target = dict(address=address, device_id=device_id,
                  board="nano_rp2040_connect", timeout=10,
                  firmware_version=current_version() + "-m7exp3")
    with Probe(**target) as probe:
        probe.command("serial.open", channel_id=1)
        sketch_control(probe, 0xf0, 0xf1)
        triggered_at = time.monotonic()
        probe.command("serial.close", channel_id=1)
        probe.finish()
    response = managed_upload.upload(address, device_id, blink_b, 10,
                                     expected_response=b"T")
    elapsed = time.monotonic() - triggered_at
    if elapsed >= 4.5:
        raise RuntimeError("pause timeout was observed too late to attribute to the 5 s delay")
    remaining = triggered_at + 5.3 - time.monotonic()
    if remaining > 0:
        time.sleep(remaining)
    with Probe(**target) as probe:
        probe.command("serial.open", channel_id=1)
        sketch_control(probe, 0xf2, 0xf3)
        echo = probe.echo(64)
        probe.command("serial.close", channel_id=1)
        probe.finish()
    after = managed_upload.selected_hello(address, device_id)
    if after["boot_id"] != before["boot_id"]:
        raise RuntimeError("board rebooted during bounded-pause test")
    print(json.dumps({"result": "bounded_pause_refused_before_flash",
                      "upload_response": response.decode(), "seconds_to_refusal": elapsed,
                      "same_boot": True, "pause_probe_still_running": True,
                      "post_refusal_echo": echo}, indent=2))
    print("Restore Blink B with upload.py while D2 remains grounded.")


if __name__ == "__main__":
    main()
