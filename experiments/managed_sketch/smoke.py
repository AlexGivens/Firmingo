#!/usr/bin/env python3
"""Nondestructive exact-byte check of a selected managed-sketch resident."""

import argparse
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.session_smoke import Probe  # noqa: E402
from tools.version import current_version  # noqa: E402

CONSOLE_COUNTERS = ("sketch_input_discarded", "sketch_output_discarded",
                    "sketch_output_rejected")


def check_console_counters(before, after):
    for name in CONSOLE_COUNTERS:
        if name not in before or name not in after:
            raise RuntimeError(f"managed console diagnostic {name} is missing")
        if after[name] != before[name]:
            raise RuntimeError(f"managed console {name} changed during exact echo: "
                               f"{before[name]} -> {after[name]}")
    peak = after.get("sketch_output_peak")
    if not isinstance(peak, int) or not 0 <= peak <= 256:
        raise RuntimeError(f"managed console output peak is invalid: {peak!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", required=True, help="numeric IPv4 address of selected Nano")
    parser.add_argument("--device-id", required=True,
                        help="16-digit device ID checked against the selected board's prior record")
    parser.add_argument("--sketch", choices=("blink_a", "blink_b"), required=True,
                        help="operator-recorded image; not remotely attested by this protocol")
    parser.add_argument("--resident-version", choices=("m7exp1", "m7exp2", "m7exp3", "m8exp1", "m8exp2", "m8exp3", "m8exp4", "m9exp1"),
                        default="m7exp1", help="observed experimental resident generation")
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9a-fA-F]{16}", args.device_id):
        parser.error("--device-id must be exactly 16 hexadecimal digits")
    target = dict(address=args.address, device_id=args.device_id,
                  board="nano_rp2040_connect", timeout=20,
                  firmware_version=current_version() + "-" + args.resident_version)
    if args.resident_version == "m9exp1":
        from experiments.managed_sketch.fmgo_upload import TARGET
        target["targets"] = [TARGET]
    with Probe(**target) as probe:
        identity = probe.hello
        probe.command("serial.open", channel_id=1)
        before_diagnostics = (probe.command("device.diagnostics")
                              if args.resident_version.startswith(("m8", "m9")) else None)
        with Probe(**target) as second:
            if second.hello["boot_id"] != identity["boot_id"]:
                raise RuntimeError("boot changed during concurrent session check")
            second.command("serial.open", expected_error="busy", channel_id=1)
            first_echo = probe.echo(16384, seed=0x4d375052)
            slow_echo = probe.echo(4096, slow_read=True, seed=0x4d375053)
            status = probe.command("serial.status", channel_id=1)
            if (status.get("backend_rx_bytes") != 20480
                    or status.get("backend_tx_bytes") != 20480):
                raise RuntimeError(f"unexpected application byte counts: {status!r}")
            diagnostics = probe.command("device.diagnostics")
            if before_diagnostics is not None:
                check_console_counters(before_diagnostics, diagnostics)
            probe.command("serial.close", channel_id=1)
            second.command("serial.open", channel_id=1)
            transfer_echo = second.echo(4096, seed=0x4d375054)
            second.command("serial.close", channel_id=1)
            second.finish()
        probe.finish()
    with Probe(**target) as reconnected:
        if reconnected.hello["boot_id"] != identity["boot_id"]:
            raise RuntimeError("boot changed during fresh-session reconnect check")
        reconnected.command("serial.open", channel_id=1)
        reconnect_echo = reconnected.echo(4096, seed=0x4d375055)
        reconnected.command("serial.close", channel_id=1)
        reconnected.finish()
    print(json.dumps({"resident_version": identity["firmware_version"],
                      "device_id": identity["device_id"],
                      "boot_id": identity["boot_id"],
                      "operator_selected_sketch": args.sketch,
                      "before_diagnostics": before_diagnostics,
                      "diagnostics": diagnostics,
                      "first_echo": first_echo, "slow_echo": slow_echo,
                      "ownership_transfer_echo": transfer_echo,
                      "reconnect_echo": reconnect_echo}, indent=2))
    print("Sketch variant is an operator claim; observe its LED rate separately.")


if __name__ == "__main__":
    main()
