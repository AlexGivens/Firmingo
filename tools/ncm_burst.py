"""Bounded two-connection hardware check for the NCM receive-worker budget path."""

import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import json
import re
import sys
import threading
import time
import traceback

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.session_smoke import HEADER, Probe, decode_header, frame
from tools.soak import diagnostics


def invalid_diagnostics_frames(peer, count=16, payload_bytes=3072):
    """Create large FMGO frames with valid JSON and a recoverable wrong schema."""
    frames = []
    request_ids = []
    for _ in range(count):
        request_id = peer.next_request
        peer.next_request += 1
        request_ids.append(request_id)
        prefix = b'{"op":"device.diagnostics","channel_id":1'
        suffix = b'}'
        payload = prefix + b' ' * (payload_bytes - len(prefix) - len(suffix)) + suffix
        frames.append(frame(1, request_id, payload))
    return b"".join(frames), request_ids


def send_invalid_burst(peer, barrier, count, payload_bytes):
    wire, request_ids = invalid_diagnostics_frames(peer, count, payload_bytes)
    barrier.wait()
    started = time.monotonic()
    peer.sock.settimeout(peer.timeout)
    peer.sock.sendall(wire)
    responses = []
    deadline = started + peer.timeout
    for expected_id in request_ids:
        kind, actual_id, size = decode_header(peer._exact(HEADER.size, deadline))
        payload = json.loads(peer._exact(size, deadline))
        code = payload.get("error", {}).get("code") if isinstance(payload, dict) else None
        if kind != 2 or actual_id != expected_id or payload.get("ok") is not False or code != "invalid_argument":
            raise RuntimeError(f"unexpected burst response for request {expected_id}: {payload!r}")
        responses.append({"request_id": actual_id, "error": code})
    return {
        "wire_bytes": len(wire),
        "requests": len(request_ids),
        "responses": len(responses),
        "seconds": time.monotonic() - started,
    }


def parse_args():
    parser = argparse.ArgumentParser(
        description=(
            "Send one bounded two-connection FMGO burst and require exact owner bytes. "
            "Exit 2 means the NCM budget branch was not exercised; the command never retries."
        )
    )
    parser.add_argument("--address", required=True)
    parser.add_argument("--device-id", required=True)
    parser.add_argument("--board", required=True, choices=["nano_rp2040_connect"])
    parser.add_argument("--firmware-sha256", required=True)
    parser.add_argument("--output", required=True, help="new JSON evidence path; never overwritten")
    parser.add_argument("--timeout", type=int, default=30, metavar="1..60")
    parser.add_argument("--control-frames", type=int, default=64, metavar="1..64")
    parser.add_argument("--control-payload", type=int, default=256, metavar="128..4096")
    parser.add_argument("--echo-bytes", type=int, default=65536, metavar="1..262144")
    return parser.parse_args()


def main():
    args = parse_args()
    if not re.fullmatch(r"[0-9a-f]{64}", args.firmware_sha256):
        raise SystemExit("firmware SHA-256 must be 64 lowercase hex digits")
    for name, value, low, high in (
        ("timeout", args.timeout, 1, 60),
        ("control frames", args.control_frames, 1, 64),
        ("control payload", args.control_payload, 128, 4096),
        ("echo bytes", args.echo_bytes, 1, 262144),
    ):
        if not low <= value <= high:
            raise SystemExit(f"{name} must be {low}..{high}")
    output = Path(args.output)
    report = {
        "status": "running",
        "purpose": "single bounded concurrent burst intended to exercise the NCM receive-worker budget path",
        "address": args.address,
        "device_id": args.device_id,
        "board": args.board,
        "declared_firmware_sha256": args.firmware_sha256,
        "firmware_hash_verified_on_device": False,
        "control_frames": args.control_frames,
        "control_payload_bytes": args.control_payload,
        "echo_bytes": args.echo_bytes,
        "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }
    exit_code = 1
    try:
        with output.open("x"):
            pass
        with Probe(args.address, args.device_id, args.board, timeout=args.timeout) as owner, Probe(
            args.address, args.device_id, args.board, timeout=args.timeout
        ) as observer:
            report["hello"] = owner.hello
            if observer.hello["boot_id"] != owner.hello["boot_id"]:
                raise RuntimeError("boot ID changed between concurrent connections")
            report["initial_diagnostics"] = diagnostics(observer)
            owner.command("serial.open", channel_id=1)
            barrier = threading.Barrier(2)

            def exact_echo():
                barrier.wait()
                return owner.echo(args.echo_bytes, slow_read=True, seed=0x4E434D34)

            with ThreadPoolExecutor(max_workers=2) as pool:
                echo_work = pool.submit(exact_echo)
                burst_work = pool.submit(
                    send_invalid_burst,
                    observer,
                    barrier,
                    args.control_frames,
                    args.control_payload,
                )
                report["echo"] = echo_work.result()
                report["control_burst"] = burst_work.result()

            report["backend_status"] = owner.command("serial.status", channel_id=1)
            expected_status = {
                "channel_id": 1,
                "backend_rx_bytes": args.echo_bytes,
                "backend_tx_bytes": args.echo_bytes,
                "pending_to_backend": 0,
                "pending_to_peer": 0,
            }
            if report["backend_status"] != expected_status:
                raise RuntimeError(f'backend status mismatch: {report["backend_status"]!r}')
            report["final_diagnostics"] = diagnostics(observer, report["initial_diagnostics"])
            owner.command("serial.close", channel_id=1)
            owner.finish()
            observer.finish()

            before = report["initial_diagnostics"]
            after = report["final_diagnostics"]
            exhausted = after["ncm_budget_exhaustions"] - before["ncm_budget_exhaustions"]
            wakes = after["ncm_wake_requests"] - before["ncm_wake_requests"]
            report["counter_delta"] = {
                "ncm_budget_exhaustions": exhausted,
                "ncm_wake_requests": wakes,
            }
            if exhausted == 0 and wakes == 0:
                report["status"] = "not_exercised"
                report["note"] = "byte/control checks passed, but the NCM budget branch did not run"
                exit_code = 2
            elif exhausted != wakes:
                raise RuntimeError(f"budget/wake counter mismatch: {exhausted} != {wakes}")
            else:
                report["status"] = "passed"
                exit_code = 0
    except Exception as exc:
        report["status"] = "failed"
        report["error"] = repr(exc)
        traceback.print_exc()
    finally:
        report["finished_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        if output.exists():
            output.write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, indent=2))
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
