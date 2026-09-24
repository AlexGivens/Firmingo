from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import json
import subprocess
import threading
import time
import traceback

from tools.session_smoke import Probe
from tools.soak import diagnostics


report = {
    "firmware_sha256": "7665d4fc0c23494e8a1954ec95541d64a1ff8d4960a7219d33ba3e3664ae2192",
    "board": "nano_rp2040_connect",
    "device_id": "a1b2c3d4e5f60718",
    "address": "192.168.77.1",
    "expected_boot_id": "83c59bf30e43f49e",
    "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    "status": "failed",
    "purpose": "one unretried instrumented 64 KiB v5 application-stream transfer overlapping fresh IPv4 Internet access",
}

try:
    report["default_route"] = subprocess.run(
        ["route", "-n", "get", "default"], capture_output=True, text=True, check=True
    ).stdout
    report["board_route"] = subprocess.run(
        ["route", "-n", "get", report["address"]], capture_output=True, text=True, check=True
    ).stdout

    with Probe(report["address"], report["device_id"], report["board"], timeout=20) as peer:
        report["hello"] = peer.hello
        if peer.hello["boot_id"] != report["expected_boot_id"]:
            raise RuntimeError("boot ID changed before overlap check")
        report["initial_diagnostics"] = diagnostics(peer)
        peer.command("serial.open", channel_id=1)
        started = threading.Event()

        def transfer():
            result = {"begin_monotonic": time.monotonic()}
            started.set()
            try:
                result["result"] = peer.echo(65536, slow_read=True)
                result["status"] = "passed"
            except Exception as exc:
                result["status"] = "failed"
                result["error"] = repr(exc)
            result["end_monotonic"] = time.monotonic()
            return result

        with ThreadPoolExecutor(max_workers=1) as pool:
            work = pool.submit(transfer)
            if not started.wait(1):
                raise RuntimeError("overlap transfer did not start")
            begin = time.monotonic()
            internet = subprocess.run(
                [
                    "curl",
                    "-4",
                    "--fail",
                    "--silent",
                    "--show-error",
                    "--max-time",
                    "10",
                    "--output",
                    "/dev/null",
                    "--write-out",
                    "%{http_code} %{remote_ip}",
                    f"https://example.com/?firmingo_application_v5={time.time_ns()}",
                ],
                capture_output=True,
                text=True,
                timeout=12,
            )
            report["https"] = {
                "begin_monotonic": begin,
                "end_monotonic": time.monotonic(),
                "exit_code": internet.returncode,
                "output": internet.stdout,
                "error": internet.stderr,
            }
            report["transfer"] = work.result()

        transfer_result = report["transfer"]
        https = report["https"]
        report["internet_contained_in_transfer"] = (
            transfer_result["begin_monotonic"]
            <= https["begin_monotonic"]
            <= https["end_monotonic"]
            <= transfer_result["end_monotonic"]
        )
        if transfer_result["status"] != "passed":
            raise RuntimeError(transfer_result["error"])
        if internet.returncode or not report["internet_contained_in_transfer"]:
            raise RuntimeError("concurrent Internet check failed")

        report["backend_status"] = peer.command("serial.status", channel_id=1)
        expected = {
            "channel_id": 1,
            "backend_rx_bytes": 65536,
            "backend_tx_bytes": 65536,
            "pending_to_backend": 0,
            "pending_to_peer": 0,
        }
        if report["backend_status"] != expected:
            raise RuntimeError(f'backend status mismatch: {report["backend_status"]!r}')
        report["final_diagnostics"] = diagnostics(peer, report["initial_diagnostics"])
        for field in (
            "application_rx_pending",
            "application_tx_pending",
            "application_rx_discarded",
            "application_tx_discarded",
        ):
            if report["final_diagnostics"][field] != 0:
                raise RuntimeError(f'unexpected {field}: {report["final_diagnostics"][field]}')
        peer.command("serial.close", channel_id=1)
        peer.finish()
        report["status"] = "passed"
except Exception as exc:
    report["error"] = repr(exc)
    traceback.print_exc()
finally:
    report["finished_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    Path("build/application-v5-overlap.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))

if report["status"] != "passed":
    raise SystemExit(1)
