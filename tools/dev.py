#!/usr/bin/env python3
"""Explicit build, test and bootloader-only flash operations for Firmingo."""

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
BOARDS = ("nano_rp2040_connect", "raspberry_pi_pico")
REFERENCE_SKETCHES = {
    "local-only": "examples/firmingo_local_only",
    "application": "examples/firmingo_application",
    "uart": "examples/firmingo_uart",
}


def manifest_path(board):
    return ROOT / "boards" / board / "baseline.json"


def load_manifest(board):
    manifest = json.loads(manifest_path(board).read_text())
    if manifest.get("board") != board:
        raise RuntimeError(f"board manifest identity mismatch: expected {board}")
    return manifest


def selected_sketch(manifest, firmware):
    if firmware in REFERENCE_SKETCHES:
        return ROOT / REFERENCE_SKETCHES[firmware]
    if "sketch" not in manifest:
        raise RuntimeError(f"{manifest['board']} has no preserved baseline firmware")
    return ROOT / manifest["sketch"]


def tool(name, variable):
    configured = os.environ.get(variable)
    if configured:
        return configured
    local = Path(sys.executable).parent / name
    return str(local) if local.is_file() else name


def run(command, timeout=120, capture=False):
    print("+ " + shlex.join(map(str, command)), flush=True)
    result = subprocess.run(list(map(str, command)), cwd=ROOT, timeout=timeout,
                            text=True, stdout=subprocess.PIPE if capture else None,
                            stderr=subprocess.STDOUT if capture else None)
    if result.returncode:
        if capture:
            print(result.stdout, file=sys.stderr)
        raise RuntimeError(f"command exited {result.returncode}; rerun command shown above")
    return result.stdout if capture else None


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_baseline(manifest):
    if "sketch" not in manifest or "sketch_sha256" not in manifest:
        return
    sketch = ROOT / manifest["sketch"] / "RP2040_USB_NCM_POC.ino"
    if digest(sketch) != manifest["sketch_sha256"]:
        raise RuntimeError("preserved baseline hash changed; review before updating the manifest")


def check_arduino(manifest):
    cli = tool("arduino-cli", "ARDUINO_CLI")
    command = [cli]
    if os.environ.get("ARDUINO_CONFIG_FILE"):
        command += ["--config-file", os.environ["ARDUINO_CONFIG_FILE"]]
    version = json.loads(run(command + ["version", "--json"], capture=True))
    if version.get("VersionString") != manifest["arduino_cli_version"]:
        raise RuntimeError(f"Arduino CLI {manifest['arduino_cli_version']} required; observed {version}")
    cores = json.loads(run(command + ["core", "list", "--json"], capture=True))
    installed = next((p.get("installed_version") for p in cores.get("platforms", [])
                      if p["id"] == manifest["core"]), None)
    if installed != manifest["core_version"]:
        raise RuntimeError(f"need {manifest['core']}@{manifest['core_version']}; observed {installed}")
    return command


def effective_arduino_config(cli):
    # dump reports only explicit overrides. Query CLI defaults rather than
    # assuming platform-specific Arduino data/user locations (including CI).
    config = json.loads(run(cli + ["config", "dump", "--json"], capture=True))["config"]
    directories = config.setdefault("directories", {})
    for key in ("data", "user", "downloads"):
        if key not in directories:
            directories[key] = json.loads(run(cli + ["config", "get", "directories." + key, "--json"], capture=True))
    if not directories["data"]:
        raise RuntimeError("Arduino CLI did not report its data directory")
    return config


def doctor(_args):
    problems = 0
    for label, command, remedy in [
        ("CMake", [tool("cmake", "CMAKE"), "--version"], "install requirements-dev.txt in a venv"),
        ("CTest", [tool("ctest", "CTEST"), "--version"], "install requirements-dev.txt in a venv"),
        ("C++ compiler", [os.environ.get("CXX", "c++"), "--version"], "install a native C++ toolchain; see docs/development.md"),
        ("pytest", [sys.executable, "-m", "pytest", "--version"], "install requirements-dev.txt in a venv"),
        ("patch", ["patch", "--version"], "install patch for the pinned USB build overlay; see docs/usb-startup.md"),
    ]:
        try:
            output = run(command, capture=True)
            print(f"OK {label}: {output.splitlines()[0]}")
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
            problems += 1
            print(f"MISSING/BROKEN {label}: {exc}; {remedy}")
    try:
        manifests = [load_manifest(board) for board in BOARDS]
        for manifest in manifests:
            check_baseline(manifest)
        check_arduino(manifests[0])
        print("OK board manifests, baseline checksum, Arduino CLI and core pins")
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as exc:
        problems += 1
        print(f"MISSING/BROKEN Arduino build: {exc}; see docs/development.md")
    print("Hardware: SKIPPED (no connection attempted). Compiler version checks do not establish a build pass.")
    return 1 if problems else 0


def test(args):
    build = ROOT / "build" / ("native-sanitize" if args.sanitize else "native")
    cmake = tool("cmake", "CMAKE")
    run([cmake, "-S", ROOT, "-B", build, "-DCMAKE_BUILD_TYPE=Debug",
         f"-DFIRMINGO_SANITIZE={'ON' if args.sanitize else 'OFF'}"])
    run([cmake, "--build", build, "--parallel", "2"])
    run([tool("ctest", "CTEST"), "--test-dir", build, "--output-on-failure"])
    run([sys.executable, "-m", "pytest", "tests/host", "-q"])
    return 0


def build(args):
    manifest = load_manifest(args.board)
    check_baseline(manifest)
    sketch = selected_sketch(manifest, args.firmware)
    cli = check_arduino(manifest)
    out = ROOT / "build" / "firmware" / args.board
    if args.firmware in REFERENCE_SKETCHES:
        out = out / args.firmware
    out.mkdir(parents=True, exist_ok=True)
    usb_overlay = None
    if args.firmware in REFERENCE_SKETCHES:
        from tools.usb_core import prepare
        config = effective_arduino_config(cli)
        cli, usb_overlay = prepare(ROOT, cli, config, out / "usb-toolchain")
    # Separate output from Arduino's intermediate compilation directory.
    command = cli + ["compile", "--fqbn", manifest["fqbn"], "--warnings", "all",
               "--build-path", str(out / "work"), "--output-dir", str(out / "artifacts"),
               str(sketch)]
    if args.firmware in REFERENCE_SKETCHES:
        command += ["--library", str(ROOT), "--build-property",
                    f"compiler.cpp.extra_flags=-DFIRMINGO_NET_C={args.subnet_octet} -DFIRMINGO_USB_DEFERRED_START -DDISABLE_USB_SERIAL"]
    if args.verbose:
        command.append("--verbose")
    # Remove only stale exported images; never mislabel an earlier image as new.
    artifacts = out / "artifacts"
    if artifacts.exists():
        shutil.rmtree(artifacts)
    print("+ " + shlex.join(command), flush=True)
    sources = [p for p in sketch.rglob("*") if p.is_file() and not p.name.startswith(".")]
    if args.firmware in REFERENCE_SKETCHES:
        sources += [p for p in (ROOT / "src").rglob("*") if p.is_file() and not p.name.startswith(".")]
        sources += [manifest_path(args.board), ROOT / "tools/usb_core.py", ROOT / "tools/dev.py"]
        sources += [p for p in (ROOT / "patches/arduino-pico-6.0.0-usb-startup").rglob("*")
                    if p.is_file() and not p.name.startswith(".")]
    report = {"manifest": manifest, "firmware": args.firmware,
              "source_sha256": {str(p.relative_to(ROOT)): digest(p) for p in sources},
              "command": command, "status": "failed", "artifacts": {}}
    if args.firmware in REFERENCE_SKETCHES:
        report["usb_profile"] = "ncm-only"
        report["usb_startup_overlay"] = usb_overlay
        report["board_address"] = f"192.168.{args.subnet_octet}.1"
    try:
        with (out / "build.log").open("w") as log:
            completed = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                       timeout=600, text=True)
        log_text = (out / "build.log").read_text()
        print(log_text)
        report["exit_code"] = completed.returncode
        if completed.returncode:
            raise RuntimeError(f"firmware compilation failed; see {out / 'build.log'}")
        for path in artifacts.iterdir():
            if path.is_file():
                report["artifacts"][path.name] = {"sha256": digest(path), "bytes": path.stat().st_size}
        flash = re.search(r"Sketch uses (\d+) bytes", log_text)
        ram = re.search(r"Global variables use (\d+) bytes", log_text)
        if not flash or not ram:
            raise RuntimeError("build completed but size summary unavailable; budgets not verified")
        report["flash_bytes"] = int(flash[1])
        report["static_ram_bytes"] = int(ram[1])
        if int(flash[1]) > manifest["flash_budget_bytes"] or int(ram[1]) > manifest["static_ram_budget_bytes"]:
            raise RuntimeError("firmware exceeds initial board memory budget")
        report["status"] = "compile-tested"
    finally:
        (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Build report: {out / 'report.json'}; hardware NOT tested")
    return 0


def smoke(args):
    from tools.smoke import validate_target
    address = validate_target(args.address)
    if args.firmware in ("application", "uart"):
        if not args.device_id or args.allow_legacy_no_identity:
            raise RuntimeError(f"{args.firmware} smoke requires --device-id and disallows legacy identity bypass")
        if args.firmware == "uart":
            from tools.uart_smoke import smoke as uart_smoke
            return uart_smoke(address, args.device_id, args.board)
        from tools.session_smoke import smoke as application_smoke
        return application_smoke(address, args.device_id, args.board)
    if not args.device_id and not args.allow_legacy_no_identity:
        raise RuntimeError("select --device-id for qualification firmware, or --allow-legacy-no-identity for the preserved prototype")
    command = [sys.executable, "-m", "pytest", "tests/hardware", "-v", "-s", "--board", args.board, "--address", address]
    if args.device_id:
        command += ["--device-id", args.device_id]
    else:
        print("LEGACY MODE: device identity is unverified. Source connections may replace a prototype client.")
        command.append("--allow-legacy-no-identity")
    run(command, timeout=90)
    return 0


def soak(args):
    from tools.soak import run as sustained
    return sustained(args)


def attach(args):
    from tools.attach import run as qualify
    return qualify(args)


def flash(args):
    # No serial reset/upload helper: the user must select an already-mounted
    # ROM bootloader so the old network firmware never needs to start.
    mount = Path(args.mount).resolve()
    info = (mount / "INFO_UF2.TXT").read_text()
    if "Board-ID: RPI-RP2" not in info:
        raise RuntimeError("selected volume does not identify an RP2040 ROM bootloader")
    out = ROOT / "build" / "firmware" / args.board / args.firmware
    report = json.loads((out / "report.json").read_text())
    if report.get("status") != "compile-tested" or report.get("firmware") != args.firmware:
        raise RuntimeError(f"a successful {args.firmware} build report is required")
    for name, sha in report["source_sha256"].items():
        if digest(ROOT / name) != sha:
            raise RuntimeError(f"source changed since build: {name}; rebuild first")
    image = out / "artifacts" / (Path(REFERENCE_SKETCHES[args.firmware]).name + ".ino.uf2")
    if digest(image) != report["artifacts"][image.name]["sha256"]:
        raise RuntimeError("UF2 hash does not match build report")
    print(f"Copying verified {args.firmware} firmware to explicitly selected {mount}", flush=True)
    with image.open("rb") as source, (mount / "FIRMINGO.UF2").open("wb") as target:
        shutil.copyfileobj(source, target)
        target.flush()
        os.fsync(target.fileno())
    print("UF2 copied; boot/network success still requires verification.")
    return 0


def package(args):
    from tools.package_release import package as assemble
    destination = assemble(ROOT, tuple(args.board or BOARDS), tuple(args.firmware or ["application", "uart"]))
    print(f"Release package: {destination} and {destination}.zip; "
          "exact images are compile-tested, not newly hardware-qualified")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    p = commands.add_parser("doctor", help="check tools without installing or contacting hardware")
    p.set_defaults(function=doctor)
    p = commands.add_parser("test", help="native production core and host harness regression tests")
    p.add_argument("--sanitize", action="store_true", help="enable address/undefined-behavior sanitizers")
    p.set_defaults(function=test)
    p = commands.add_parser("build", help="compile a selected firmware profile; never upload")
    p.add_argument("--board", required=True, choices=BOARDS)
    p.add_argument("--firmware", choices=["baseline", *REFERENCE_SKETCHES], default="baseline")
    p.add_argument("--subnet-octet", type=int, choices=range(256), default=77,
                   metavar="0..255", help="reference builds: 192.168.X.0/24 (default 77)")
    p.add_argument("--verbose", action="store_true")
    p.set_defaults(function=build)
    p = commands.add_parser("smoke", help="explicit-target, nondestructive hardware suite")
    p.add_argument("--board", required=True, choices=BOARDS)
    p.add_argument("--address", required=True, help="numeric IPv4 address; no implicit default")
    p.add_argument("--device-id", help="expected board ID from verified diagnostics; checked before stream traffic")
    p.add_argument("--allow-legacy-no-identity", action="store_true")
    p.add_argument("--firmware", choices=["baseline", *REFERENCE_SKETCHES], default="local-only")
    p.set_defaults(function=smoke)
    p = commands.add_parser("soak", help="opt-in stream traffic and optional memory samples")
    p.add_argument("--board", required=True, choices=BOARDS)
    p.add_argument("--address", required=True)
    p.add_argument("--device-id", required=True)
    p.add_argument("--firmware-sha256", required=True, help="declared flashed UF2 hash; not verified on-device")
    p.add_argument("--firmware", choices=["application", "uart"], default="application",
                   help="FMGO backend profile expected from hello (default application)")
    p.add_argument("--output", required=True, help="new JSON evidence file; existing files never overwritten")
    p.add_argument("--duration", type=int, default=60, help="minimum run duration in seconds, 1..3600; finish current bounded batch/session")
    p.add_argument("--byte-count", type=int, default=16384, help="exact bytes per batch, 1..262144")
    p.add_argument("--batch-timeout", type=int, default=20, help="per echo/control/connect timeout seconds, 1..60")
    p.add_argument("--reconnect-every", type=int, default=4, help="complete batches per session, 1..1000")
    p.add_argument("--diagnostics", action="store_true", help="require firmware memory/queue samples; no fallback on unsupported firmware")
    p.add_argument("--seed", type=lambda value: int(value, 0), default=0x4653474f, help="uint32 payload seed, decimal or 0x hex")
    p.set_defaults(function=soak)
    p = commands.add_parser("attach", help="macOS: observe physical USB reconnections and qualify local-only networking")
    p.add_argument("--board", required=True, choices=BOARDS)
    p.add_argument("--address", required=True, help="numeric board IPv4 address")
    p.add_argument("--device-id", required=True, help="expected board USB/diagnostic ID")
    p.add_argument("--interface", required=True, help="actual Nano Ethernet interface, such as en5")
    p.add_argument("--output", required=True, help="new JSON evidence file; existing files are never overwritten")
    p.add_argument("--cycles", type=int, choices=range(1,21), default=1, metavar="1..20")
    p.add_argument("--start-cycle", type=int, default=1)
    p.add_argument("--unplug-timeout", type=float, default=120, help="seconds to wait for user unplug, default 120")
    p.add_argument("--plug-timeout", type=float, default=120, help="seconds to wait for user reconnect, default 120")
    p.add_argument("--ready-timeout", type=float, default=30, help="network/identity readiness deadline, not a performance guarantee")
    p.add_argument("--firmware-sha256", required=True, help="hash of the manually selected flashed UF2; not verified on-device")
    p.add_argument("--connection-details", default="board revision and cable/hub not recorded", help="user-reported board revision and cable/hub")
    p.set_defaults(function=attach)
    p = commands.add_parser("flash", help="copy a verified reference UF2 to an already-mounted RP2040 ROM bootloader")
    p.add_argument("--board", required=True, choices=BOARDS)
    p.add_argument("--firmware", choices=list(REFERENCE_SKETCHES), default="local-only")
    p.add_argument("--mount", required=True, help="explicit bootloader volume path, e.g. /Volumes/RPI-RP2")
    p.set_defaults(function=flash)
    p = commands.add_parser("package", help="verify current builds and stage board-specific beta UF2s under dist/")
    p.add_argument("--board", action="append", choices=BOARDS, help="board ID; repeat; default both supported boards")
    p.add_argument("--firmware", action="append", choices=list(("application", "uart")),
                   help="reference backend; repeat; default application and uart")
    p.set_defaults(function=package)
    args = parser.parse_args()
    try:
        return args.function(args)
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as exc:
        print(f"ERROR: {exc}\nRerun: {shlex.join([sys.executable, *sys.argv])}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.path.insert(0, str(ROOT))
    raise SystemExit(main())
