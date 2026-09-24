#!/usr/bin/env python3
"""Induce one bounded Nano UART overrun with the Pico peer and verify recovery."""

import argparse
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.session_smoke import Probe
from tools.uart_peer_smoke import (PicoPeer, fields, pattern, receive_serial,
                                  PEER_VERSION, require_peer_event, send_serial)
from tools.uart_smoke import FIRMWARE_VERSION, validate_channel


def run(args):
    output = Path(args.output)
    if output.exists():
        raise RuntimeError(f'output already exists: {output}')
    options = dict(port=args.network_port, firmware_version=FIRMWARE_VERSION,
                   backend='uart', timeout=args.timeout)
    report = dict(status='failed', board=args.board, address=args.address,
                  device_id=args.device_id, peer_port=args.peer_port,
                  burst_bytes=args.burst_bytes, recovery_bytes=args.recovery_bytes,
                  seed=args.seed)
    try:
        with PicoPeer(args.peer_port, args.timeout) as peer:
            identity = peer.command('PING')
            if fields(identity).get('peer') != PEER_VERSION:
                raise RuntimeError(f'unexpected Pico identity: {identity!r}')
            report['peer_identity'] = identity
            peer.command('CLEAR')

            # Establish an empty UART before the intentional unsolicited burst.
            with Probe(args.address, args.device_id, args.board, **options) as cleanup:
                validate_channel(cleanup.hello['channels'][0])
                report['nano_hello'] = cleanup.hello
                cleanup.command('serial.open', channel_id=1)
                cleanup.command('serial.close', channel_id=1)
                report['baseline'] = cleanup.command('device.diagnostics')
                cleanup.finish()

            peer.command(f'SEND {args.burst_bytes} {args.seed}')
            report['burst_event'] = peer.event('send_done', args.timeout)
            time.sleep(0.25)

            with Probe(args.address, args.device_id, args.board, **options) as observer:
                overflow = observer.command('device.diagnostics')
                report['after_burst'] = overflow
                observer.finish()
            baseline = report['baseline']
            if (overflow.get('uart_rx_overrun_events', 0)
                    <= baseline.get('uart_rx_overrun_events', 0)):
                raise RuntimeError(f'intentional burst did not report an overrun: {overflow!r}')
            if (overflow.get('uart_rx_lost_bytes_minimum', 0)
                    <= baseline.get('uart_rx_lost_bytes_minimum', 0)):
                raise RuntimeError(f'intentional burst did not report minimum loss: {overflow!r}')
            if not 1 <= overflow.get('uart_rx_pending', 0) <= 256:
                raise RuntimeError(f'intentional burst did not leave bounded RX pending: {overflow!r}')

            with Probe(args.address, args.device_id, args.board, **options) as nano:
                nano.command('serial.open', channel_id=1)
                after_acquire = nano.command('device.diagnostics')
                report['after_acquire'] = after_acquire
                if after_acquire.get('uart_rx_pending') != 0:
                    raise RuntimeError(f'acquisition did not drain stale RX: {after_acquire!r}')
                if (after_acquire.get('uart_rx_discarded', 0)
                        <= baseline.get('uart_rx_discarded', 0)):
                    raise RuntimeError(f'acquisition did not count discarded RX: {after_acquire!r}')
                for name in ('uart_rx_overrun_events', 'uart_rx_lost_bytes_minimum'):
                    if after_acquire.get(name) != overflow.get(name):
                        raise RuntimeError(f'{name} did not persist through acquisition')

                peer.command('CLEAR')
                peer.command('MODE SINK')
                receive_seed = args.seed ^ 0xa5a5a5a5
                peer.command(f'EXPECT {args.recovery_bytes} {receive_seed}')
                send_serial(nano, pattern(args.recovery_bytes, receive_seed))
                require_peer_event(peer.event('expect_done', args.timeout),
                                   args.recovery_bytes)

                transmit_seed = receive_seed ^ 0xffffffff
                expected = pattern(args.recovery_bytes, transmit_seed)
                peer.command(f'SEND {args.recovery_bytes} {transmit_seed}')
                received = receive_serial(nano, args.recovery_bytes)
                require_peer_event(peer.event('send_done', args.timeout),
                                   args.recovery_bytes)
                if received != expected:
                    offset = next(i for i, pair in enumerate(zip(received, expected))
                                  if pair[0] != pair[1])
                    raise RuntimeError(f'recovery mismatch at offset {offset}')

                status = nano.command('serial.status', channel_id=1)
                final = nano.command('device.diagnostics')
                report['recovery_status'] = status
                report['final_diagnostics'] = final
                report['peer_final'] = peer.command('STATS')
                if (status.get('backend_rx_bytes') != args.recovery_bytes
                        or status.get('backend_tx_bytes') != args.recovery_bytes
                        or status.get('pending_to_backend') != 0
                        or status.get('pending_to_peer') != 0):
                    raise RuntimeError(f'recovery counters failed: {status!r}')
                for name in ('uart_rx_overrun_events', 'uart_rx_lost_bytes_minimum',
                             'uart_rx_discarded', 'tcp_errors'):
                    if final.get(name) != after_acquire.get(name):
                        raise RuntimeError(f'recovery changed {name}: {final!r}')
                nano.command('serial.close', channel_id=1)
                nano.finish()
            report['status'] = 'passed'
            print(f'Intentional overrun reported and recovered; '
                  f'{args.recovery_bytes} exact bytes each direction')
    except Exception as exc:
        report['error'] = f'{type(exc).__name__}: {exc}'
        raise
    finally:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2) + '\n')
        print(f'Evidence: {output}')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', required=True)
    parser.add_argument('--device-id', required=True)
    parser.add_argument('--board', required=True, choices=['nano_rp2040_connect'])
    parser.add_argument('--peer-port', required=True)
    parser.add_argument('--network-port', type=int, default=7420)
    parser.add_argument('--burst-bytes', type=int, default=4096)
    parser.add_argument('--recovery-bytes', type=int, default=4096)
    parser.add_argument('--seed', type=lambda value: int(value, 0), default=0x4f565252)
    parser.add_argument('--timeout', type=float, default=15)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    if not 512 <= args.burst_bytes <= 262144:
        parser.error('--burst-bytes must be 512..262144')
    if not 1 <= args.recovery_bytes <= 131072:
        parser.error('--recovery-bytes must be 1..131072')
    if not 0 <= args.seed <= 0xffffffff:
        parser.error('--seed must be an unsigned 32-bit value')
    try:
        return run(args)
    except (OSError, RuntimeError, ValueError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
