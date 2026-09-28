#!/usr/bin/env python3
"""Install one exact M7 proof capsule on a physically armed Nano resident."""

import argparse
import hashlib
import ipaddress
import json
from pathlib import Path
import re
import socket
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from experiments.managed_sketch.build_composite import checked_capsule  # noqa: E402
from experiments.managed_sketch.inspect_hello import read_hello  # noqa: E402
from tools.session_smoke import Probe  # noqa: E402
from tools.version import current_version  # noqa: E402


def sha(data):
    return hashlib.sha256(data).hexdigest()


def verified_capsule(sketch, expected_resident, expected_capsule,
                     resident_dir=None, module_dir=None):
    resident = Path(resident_dir) if resident_dir is not None else ROOT / "build/managed_sketch/resident"
    module = (Path(module_dir) if module_dir is not None else ROOT / "build/managed_sketch") / sketch
    resident_report = json.loads((resident / "report.json").read_text())
    module_report = json.loads((module / "report.json").read_text())
    if resident_report.get("board") != "nano_rp2040_connect":
        raise ValueError("resident report is for a different board")
    if resident_report.get("status") != "compile-tested experimental resident; not flashed":
        raise ValueError("unexpected resident report status")
    if resident_report.get("uf2_sha256") != expected_resident:
        raise ValueError("resident UF2 does not match the selected exact hash")
    if sha((resident / "artifacts/firmingo_managed_resident.ino.uf2").read_bytes()) != expected_resident:
        raise ValueError("resident UF2 changed since its report")
    if sha((resident / "artifacts/firmingo_managed_resident.ino.elf").read_bytes()) != resident_report["elf_sha256"]:
        raise ValueError("resident ELF changed since its report")
    if module_report.get("resident_elf_sha256") != resident_report["elf_sha256"]:
        raise ValueError("capsule was built for a different resident ELF")
    if (module_report.get("board") != "nano_rp2040_connect" or
            module_report.get("sketch") != sketch or
            module_report.get("status") != "compile-tested module only; no resident or hardware proof"):
        raise ValueError("unexpected module report identity")
    for report in (resident_report, module_report):
        for name, expected in report.get("source_sha256", {}).items():
            path = (ROOT / name).resolve()
            if not path.is_relative_to(ROOT) or not path.is_file() or sha(path.read_bytes()) != expected:
                raise ValueError(f"source changed since build: {name}")
    capsule = (module / (sketch + ".fms")).read_bytes()
    if sha(capsule) != expected_capsule or module_report.get("capsule_sha256") != expected_capsule:
        raise ValueError("capsule does not match the selected exact hash")
    if not 256 < len(capsule) <= 4096:
        raise ValueError("capsule exceeds the resident's 4 KiB proof stage")
    checked_capsule(capsule)
    return capsule


def selected_hello(address, device_id):
    hello = read_hello(address)
    expected_version = current_version() + "-m7exp3"
    if (hello.get("device_id") != device_id or
            hello.get("board_id") != "nano_rp2040_connect" or
            hello.get("firmware_version") != expected_version):
        raise RuntimeError(f"unexpected board identity or resident version: {hello!r}")
    return hello


def upload(address, device_id, capsule, timeout, expected_response=b"K"):
    header = (b"FMSU" + bytes((1, 0, 0, 0)) + len(capsule).to_bytes(4, "little") +
              hashlib.sha256(capsule).digest() + device_id.encode("ascii"))
    if len(header) != 60:
        raise AssertionError("private upload header length changed")
    frame = header + capsule + b"GO!!"
    deadline = time.monotonic() + timeout
    with socket.create_connection((address, 7421), timeout=timeout) as connection:
        for offset in range(0, len(frame), 128):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("upload send deadline expired")
            connection.settimeout(remaining)
            connection.sendall(frame[offset:offset + 128])
        connection.shutdown(socket.SHUT_WR)
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("upload result deadline expired")
        connection.settimeout(remaining)
        response = connection.recv(1)
    meanings = {b"K": "installed and read back", b"D": "physical arm absent",
                b"B": "FMGO stream connection still active",
                b"I": "invalid frame or capsule", b"T": "timeout",
                b"F": "flash write or readback failed"}
    if response != expected_response:
        raise RuntimeError(f"unexpected install response: {meanings.get(response, repr(response))}; "
                           "inspect the slot before retrying")
    return response


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", choices=("nano_rp2040_connect",), required=True)
    parser.add_argument("--address", required=True, help="selected numeric board IPv4 address")
    parser.add_argument("--device-id", required=True,
                        help="16-digit ID from this physical Nano's prior record")
    parser.add_argument("--sketch", choices=("blink_a", "blink_b", "pause_probe",
                                             "stall_probe"),
                        required=True)
    parser.add_argument("--expected-resident-uf2-sha256", required=True)
    parser.add_argument("--expected-capsule-sha256", required=True)
    parser.add_argument("--install", action="store_true",
                        help="explicitly replace the first sketch-slot sector")
    parser.add_argument("--timeout", type=float, default=10)
    args = parser.parse_args()
    try:
        address = str(ipaddress.IPv4Address(args.address))
    except ipaddress.AddressValueError as exc:
        parser.error(str(exc))
    if address in ("0.0.0.0", "255.255.255.255") or ipaddress.IPv4Address(address).is_multicast:
        parser.error("select one unicast board IPv4 address")
    device_id = args.device_id.lower()
    if not re.fullmatch(r"[0-9a-f]{16}", device_id):
        parser.error("--device-id must be exactly 16 hexadecimal digits")
    for name in ("expected_resident_uf2_sha256", "expected_capsule_sha256"):
        if not re.fullmatch(r"[0-9a-fA-F]{64}", getattr(args, name)):
            parser.error(f"--{name.replace('_', '-')} must be 64 hexadecimal digits")
    if not 2 <= args.timeout <= 30:
        parser.error("--timeout must be 2..30 seconds")
    capsule = verified_capsule(args.sketch,
                               args.expected_resident_uf2_sha256.lower(),
                               args.expected_capsule_sha256.lower())
    before = selected_hello(address, device_id)
    if not args.install:
        parser.error("--install is required after reviewing the exact capsule and arming D2-to-GND")
    upload(address, device_id, capsule, args.timeout)
    after = selected_hello(address, device_id)
    if after.get("boot_id") != before.get("boot_id"):
        raise RuntimeError("board rebooted during upload; install outcome requires inspection")
    target = dict(address=address, device_id=device_id,
                  board="nano_rp2040_connect", timeout=20,
                  firmware_version=current_version() + "-m7exp3")
    with Probe(**target) as probe:
        probe.command("serial.open", channel_id=1)
        # The diagnostic probes reserve trigger bytes 0xf0 and 0xf4. The
        # first 64 bytes of the unseeded vector contain neither; normal sketches
        # still receive the full 4 KiB exact-byte post-install check.
        echo = (probe.echo(64) if args.sketch in ("pause_probe", "stall_probe") else
                probe.echo(4096, seed=0x4d375550))
        probe.command("serial.close", channel_id=1)
        probe.finish()
    print(json.dumps({"result": "installed_and_echo_checked",
                      "sketch": args.sketch, "device_id": device_id,
                      "boot_id": after["boot_id"], "capsule_bytes": len(capsule),
                      "capsule_sha256": sha(capsule), "echo": echo}, indent=2))
    print("Observe the D13 blink rate; it is not attested by the network check.")


if __name__ == "__main__":
    main()
