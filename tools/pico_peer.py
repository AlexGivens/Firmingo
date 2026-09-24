#!/usr/bin/env python3
"""Build or bootloader-flash the test-only Raspberry Pi Pico UART peer."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SKETCH = ROOT / "tests" / "hardware" / "pico_uart_peer"
OUT = ROOT / "build" / "hardware" / "pico_uart_peer"
REPORT = OUT / "report.json"
FQBN = "rp2040:rp2040:rpipico:flash=2097152_0,freq=125,opt=Small,rtti=Disabled,exceptions=Disabled,usbstack=picosdk,ipbtstack=ipv4only"
CORE = "rp2040:rp2040"
CORE_VERSION = "6.0.0"
CLI_VERSION = "1.5.1"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def cli_command():
    cli = os.environ.get("ARDUINO_CLI", "arduino-cli")
    command = [cli]
    if os.environ.get("ARDUINO_CONFIG_FILE"):
        command += ["--config-file", os.environ["ARDUINO_CONFIG_FILE"]]
    return command


def run(command, capture=False, timeout=120):
    print("+ " + shlex.join(map(str, command)), flush=True)
    completed = subprocess.run(list(map(str, command)), cwd=ROOT, text=True,
                               stdout=subprocess.PIPE if capture else None,
                               stderr=subprocess.STDOUT if capture else None,
                               timeout=timeout)
    if completed.returncode:
        if capture:
            print(completed.stdout, file=sys.stderr)
        raise RuntimeError(f"command exited {completed.returncode}")
    return completed.stdout if capture else None


def verify_toolchain(cli):
    version = json.loads(run(cli + ["version", "--json"], capture=True))
    if version.get("VersionString") != CLI_VERSION:
        raise RuntimeError(f"Arduino CLI {CLI_VERSION} required")
    cores = json.loads(run(cli + ["core", "list", "--json"], capture=True))
    observed = next((p.get("installed_version") for p in cores.get("platforms", [])
                     if p.get("id") == CORE), None)
    if observed != CORE_VERSION:
        raise RuntimeError(f"{CORE}@{CORE_VERSION} required; observed {observed}")


def source_hashes():
    paths = [SKETCH / "pico_uart_peer.ino", SKETCH / "README.md", Path(__file__)]
    return {str(path.relative_to(ROOT)): digest(path) for path in paths}


def build(_args):
    cli = cli_command()
    verify_toolchain(cli)
    work = OUT / "work"
    artifacts = OUT / "artifacts"
    if artifacts.exists():
        shutil.rmtree(artifacts)
    OUT.mkdir(parents=True, exist_ok=True)
    command = cli + ["compile", "--fqbn", FQBN, "--warnings", "all",
                     "--build-path", str(work), "--output-dir", str(artifacts),
                     str(SKETCH)]
    report = {"fixture": "pico-uart-peer-v2", "board": "raspberry_pi_pico",
              "arduino_cli_version": CLI_VERSION, "core": CORE,
              "core_version": CORE_VERSION, "fqbn": FQBN,
              "source_sha256": source_hashes(), "command": command,
              "status": "failed", "artifacts": {}}
    try:
        print("+ " + shlex.join(command), flush=True)
        with (OUT / "build.log").open("w") as log:
            completed = subprocess.run(command, cwd=ROOT, text=True, stdout=log,
                                       stderr=subprocess.STDOUT, timeout=600)
        log_text = (OUT / "build.log").read_text()
        print(log_text)
        report["exit_code"] = completed.returncode
        if completed.returncode:
            raise RuntimeError(f"fixture compilation failed; see {OUT / 'build.log'}")
        for path in artifacts.iterdir():
            if path.is_file():
                report["artifacts"][path.name] = {
                    "sha256": digest(path), "bytes": path.stat().st_size}
        flash = re.search(r"Sketch uses (\d+) bytes", log_text)
        ram = re.search(r"Global variables use (\d+) bytes", log_text)
        if not flash or not ram:
            raise RuntimeError("size summary unavailable")
        report["flash_bytes"] = int(flash[1])
        report["static_ram_bytes"] = int(ram[1])
        report["status"] = "compile-tested"
    finally:
        REPORT.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Build report: {REPORT}; peer hardware NOT tested")


def flash(args):
    mount = Path(args.mount).resolve()
    info = (mount / "INFO_UF2.TXT").read_text()
    if "Board-ID: RPI-RP2" not in info:
        raise RuntimeError("selected volume is not an RP2040 ROM bootloader")
    report = json.loads(REPORT.read_text())
    if report.get("status") != "compile-tested":
        raise RuntimeError("a successful Pico peer build is required")
    for name, expected in report["source_sha256"].items():
        if digest(ROOT / name) != expected:
            raise RuntimeError(f"source changed since build: {name}")
    image = OUT / "artifacts" / "pico_uart_peer.ino.uf2"
    if digest(image) != report["artifacts"][image.name]["sha256"]:
        raise RuntimeError("UF2 hash does not match build report")
    print(f"Copying verified Pico UART peer to {mount}", flush=True)
    with image.open("rb") as source, (mount / "PICOPEER.UF2").open("wb") as target:
        shutil.copyfileobj(source, target)
        target.flush()
        os.fsync(target.fileno())
    print("UF2 copied; USB/UART behavior still requires verification.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("build", help="compile the peer fixture; never upload")
    command.set_defaults(function=build)
    command = commands.add_parser("flash", help="copy the verified UF2 to an explicit ROM volume")
    command.add_argument("--mount", required=True)
    command.set_defaults(function=flash)
    args = parser.parse_args()
    try:
        args.function(args)
        return 0
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as exc:
        print(f"ERROR: {exc}\nRerun: {shlex.join([sys.executable, *sys.argv])}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
