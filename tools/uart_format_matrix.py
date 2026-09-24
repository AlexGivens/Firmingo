#!/usr/bin/env python3
"""Exercise every advertised Nano UART format against the independent Pico peer."""

import argparse
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.session_smoke import Probe
from tools.uart_peer_smoke import (PEER_VERSION, PicoPeer, fields, pattern,
                                  receive_serial, require_peer_event, send_serial,
                                  uart_config)
from tools.uart_smoke import FIRMWARE_VERSION, validate_channel


def format_cases(baud=57600):
    return tuple(uart_config(baud, bits, parity, stops)
                 for bits in range(5, 9)
                 for parity in ('none', 'even', 'odd')
                 for stops in (1, 2))


def parse_peer_configuration(line):
    values = fields(line)
    try:
        result = dict(baud=int(values['baud']), actual_baud=int(values['actual_baud']),
                      data_bits=int(values['data_bits']), parity=values['parity'],
                      stop_bits=int(values['stop_bits']))
    except (KeyError, ValueError) as exc:
        raise RuntimeError(f'invalid Pico configuration response: {line!r}') from exc
    if (not 300 <= result['baud'] <= 2000000 or result['actual_baud'] <= 0
            or not 5 <= result['data_bits'] <= 8
            or result['parity'] not in ('none', 'even', 'odd')
            or result['stop_bits'] not in (1, 2)):
        raise RuntimeError(f'invalid Pico configuration response: {line!r}')
    return result


def configuration_command(config):
    return (f"CONFIG {config['baud']} {config['data_bits']} "
            f"{config['parity']} {config['stop_bits']}")


def assert_effective_configuration(result, requested, actual_baud):
    expected = dict(requested, actual_baud=actual_baud)
    if result != expected:
        raise RuntimeError(
            f'Nano effective configuration mismatch: expected {expected!r}, got {result!r}')


def observe_nano_configuration(args, options, boot_id):
    with Probe(args.address, args.device_id, args.board, **options) as observer:
        if observer.hello.get('boot_id') != boot_id:
            raise RuntimeError('Nano rebooted while observing UART configuration')
        validate_channel(observer.hello['channels'][0])
        result = observer.hello['channels'][0]['config']
        observer.finish()
    return result


def first_mismatch(actual, expected):
    return next(index for index, pair in enumerate(zip(actual, expected))
                if pair[0] != pair[1])


def assert_stable_diagnostics(current, baseline):
    for name in ('uart_rx_overrun_events', 'uart_rx_lost_bytes_minimum', 'tcp_errors'):
        if current.get(name) != baseline.get(name):
            raise RuntimeError(
                f'Nano diagnostic {name} changed from {baseline.get(name)!r} '
                f'to {current.get(name)!r}: {current!r}')
    if current.get('uart_rx_discarded', 0) < baseline.get('uart_rx_discarded', 0):
        raise RuntimeError(f'Nano discarded-byte counter regressed: {current!r}')


def check_peer_invalid_configurations(peer):
    before = parse_peer_configuration(peer.command('STATS'))
    invalid = ('CONFIG 299 8 none 1', 'CONFIG 2000001 8 none 1',
               'CONFIG 115200 4 none 1', 'CONFIG 115200 9 none 1',
               'CONFIG 115200 8 mark 1', 'CONFIG 115200 8 none 3',
               'CONFIG 115200 8 none 1 extra')
    for command in invalid:
        peer.command(command, expected_error='invalid_config')
        after = parse_peer_configuration(peer.command('STATS'))
        if after != before:
            raise RuntimeError(
                f'Pico invalid configuration changed state: {before!r} -> {after!r}')
    return len(invalid)


def check_nano_invalid_configurations(args, options, peer, boot_id, baseline):
    requested = uart_config(115200)
    peer_effective = parse_peer_configuration(peer.command(configuration_command(requested)))
    with Probe(args.address, args.device_id, args.board, **options) as nano:
        if nano.hello.get('boot_id') != boot_id:
            raise RuntimeError('Nano rebooted during invalid-configuration checks')
        validate_channel(nano.hello['channels'][0])
        nano.command('serial.open', channel_id=1, config=requested)
        effective = observe_nano_configuration(args, options, boot_id)
        assert_effective_configuration(effective, requested, peer_effective['actual_baud'])
        peer.command('CLEAR')
        invalid = (
            dict(requested, baud=299), dict(requested, baud=2000001),
            dict(requested, data_bits=4), dict(requested, data_bits=9),
            dict(requested, parity='mark'), dict(requested, stop_bits=3),
            dict(requested, flow_control='rts_cts'))
        for config in invalid:
            nano.command('serial.configure', expected_error='invalid_argument',
                         channel_id=1, config=config)
            actual = observe_nano_configuration(args, options, boot_id)
            assert_effective_configuration(actual, requested,
                                           peer_effective['actual_baud'])
        diagnostics = nano.command('device.diagnostics')
        assert_stable_diagnostics(diagnostics, baseline)
        nano.command('serial.close', channel_id=1)
        nano.finish()
    return len(invalid)


def run_case(args, options, peer, boot_id, baseline, config, count, seed):
    peer_effective = parse_peer_configuration(peer.command(configuration_command(config)))
    if any(peer_effective[name] != config[name]
           for name in ('baud', 'data_bits', 'parity', 'stop_bits')):
        raise RuntimeError(f'Pico configuration mismatch: {peer_effective!r}')
    started = time.monotonic()
    with Probe(args.address, args.device_id, args.board, **options) as nano:
        if nano.hello.get('boot_id') != boot_id:
            raise RuntimeError('Nano rebooted during UART format matrix')
        validate_channel(nano.hello['channels'][0])
        nano.command('serial.open', channel_id=1, config=config)
        effective = observe_nano_configuration(args, options, boot_id)
        assert_effective_configuration(effective, config, peer_effective['actual_baud'])

        # UART reinitialization can create a transition byte at the other peer.
        # Clear only after both UARTs have their final format.
        peer.command('CLEAR')
        peer.command('MODE SINK')
        expected = pattern(count, seed, config['data_bits'])
        peer.command(f'EXPECT {count} {seed}')
        send_serial(nano, expected)
        try:
            require_peer_event(peer.event('expect_done', args.timeout), count)
        except (OSError, RuntimeError, TimeoutError) as exc:
            raise RuntimeError(
                f"Nano-to-Pico transfer failed for {config!r}: {exc}") from exc

        reverse_seed = seed ^ 0xffffffff
        reverse = pattern(count, reverse_seed, config['data_bits'])
        peer.command(f'SEND {count} {reverse_seed}')
        try:
            received = receive_serial(nano, count)
            require_peer_event(peer.event('send_done', args.timeout), count)
        except (OSError, RuntimeError, TimeoutError) as exc:
            raise RuntimeError(
                f"Pico-to-Nano transfer failed for {config!r}: {exc}") from exc
        if received != reverse:
            raise RuntimeError(
                f"{config['data_bits']}{config['parity'][0].upper()}"
                f"{config['stop_bits']} Pico-to-Nano mismatch at "
                f'{first_mismatch(received, reverse)}')

        status = nano.command('serial.status', channel_id=1)
        if (status.get('backend_rx_bytes') != count
                or status.get('backend_tx_bytes') != count
                or status.get('pending_to_backend') != 0
                or status.get('pending_to_peer') != 0):
            raise RuntimeError(f'unexpected Nano UART counters: {status!r}')
        diagnostics = nano.command('device.diagnostics')
        assert_stable_diagnostics(diagnostics, baseline)
        peer_stats_line = peer.command('STATS')
        peer_stats = fields(peer_stats_line)
        if (peer_stats.get('rx') != str(count) or peer_stats.get('tx') != str(count)
                or peer_stats.get('overruns') != '0'
                or peer_stats.get('mismatches') != '0'):
            raise RuntimeError(f'unexpected Pico counters: {peer_stats_line!r}')
        nano.command('serial.close', channel_id=1)
        nano.finish()

    return dict(config=config, actual_baud=peer_effective['actual_baud'],
                bytes_each_direction=count, seed=seed,
                seconds=time.monotonic() - started, nano_status=status,
                nano_diagnostics=diagnostics, peer_stats=peer_stats_line)


def run(args):
    output = Path(args.output)
    if output.exists():
        raise RuntimeError(f'output already exists: {output}')
    options = dict(port=args.network_port, firmware_version=FIRMWARE_VERSION,
                   backend='uart', timeout=args.timeout)
    report = dict(status='failed', board=args.board, address=args.address,
                  device_id=args.device_id, peer_port=args.peer_port,
                  seed=args.seed, cases=[])
    try:
        with PicoPeer(args.peer_port, args.timeout) as peer:
            identity = peer.command('PING')
            if fields(identity).get('peer') != PEER_VERSION:
                raise RuntimeError(f'unexpected Pico identity: {identity!r}')
            report['peer_identity'] = identity
            report['peer_invalid_rejections'] = check_peer_invalid_configurations(peer)

            with Probe(args.address, args.device_id, args.board, **options) as observer:
                validate_channel(observer.hello['channels'][0])
                report['nano_hello'] = observer.hello
                report['baseline_diagnostics'] = observer.command('device.diagnostics')
                observer.finish()
            boot_id = report['nano_hello']['boot_id']
            baseline = report['baseline_diagnostics']
            report['nano_invalid_rejections'] = check_nano_invalid_configurations(
                args, options, peer, boot_id, baseline)

            cases = [(uart_config(300), args.boundary_bytes),
                     *((case, args.format_bytes) for case in format_cases()),
                     (uart_config(2000000), args.high_speed_bytes)]
            for index, (config, count) in enumerate(cases):
                seed = (args.seed + index * 0x9e3779b9) & 0xffffffff
                report['active_case'] = dict(config=config,
                                             bytes_each_direction=count, seed=seed)
                result = run_case(args, options, peer, boot_id, baseline,
                                  config, count, seed)
                report['cases'].append(result)
                del report['active_case']
                print(f"PASS {config['baud']} {config['data_bits']}"
                      f"{config['parity'][0].upper()}{config['stop_bits']}: "
                      f'{count} exact bytes each direction')
            report['final_diagnostics'] = report['cases'][-1]['nano_diagnostics']
            report['status'] = 'passed'
            print(f"UART format matrix passed: {len(cases)} configurations, "
                  f"{sum(case['bytes_each_direction'] for case in report['cases'])} "
                  'exact bytes in each direction')
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
    parser.add_argument('--format-bytes', type=int, default=256)
    parser.add_argument('--boundary-bytes', type=int, default=32)
    parser.add_argument('--high-speed-bytes', type=int, default=128,
                        help='bounded 2 Mbaud check; keep below the 256-byte RX FIFO')
    parser.add_argument('--seed', type=lambda value: int(value, 0), default=0x464d4154)
    parser.add_argument('--timeout', type=float, default=15)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    for name in ('format_bytes', 'boundary_bytes', 'high_speed_bytes'):
        if not 1 <= getattr(args, name) <= 131072:
            parser.error(f'--{name.replace("_", "-")} must be 1..131072')
    if not 0 <= args.seed <= 0xffffffff:
        parser.error('--seed must be an unsigned 32-bit value')
    try:
        return run(args)
    except (OSError, RuntimeError, ValueError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
