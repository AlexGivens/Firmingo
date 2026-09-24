from pathlib import Path
import json
import time
import traceback

from tools.session_smoke import Probe
from tools.soak import diagnostics


report = {
    "firmware_sha256": "155521c96f77720ad7abdbfb0d3423a2f41f24b95ebfa87e4a683a48c05e91ba",
    "board": "nano_rp2040_connect",
    "device_id": "a1b2c3d4e5f60718",
    "address": "192.168.77.1",
    "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    "status": "failed",
    "purpose": "one unretried isolated 64 KiB v4 application-stream check",
}

try:
    with Probe(report["address"], report["device_id"], report["board"], timeout=20) as peer:
        report["hello"] = peer.hello
        report["boot_id"] = peer.hello["boot_id"]
        report["initial_diagnostics"] = diagnostics(peer)
        peer.command("serial.open", channel_id=1)
        report["transfer"] = peer.echo(65536, slow_read=True)
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
    Path("build/application-v4-isolated.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))

if report["status"] != "passed":
    raise SystemExit(1)
