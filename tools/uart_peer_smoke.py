#!/usr/bin/env python3
"""Verify Nano UART bytes in each direction against the Pico peer fixture."""

import argparse
import os
from pathlib import Path
import select
import sys
import termios
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.session_smoke import Probe, decode_header, frame, HEADER
from tools.uart_smoke import FIRMWARE_VERSION, validate_channel

PEER_VERSION = 'pico-uart-peer-v2'


def pattern(count, seed, data_bits=8):
    if (not 1 <= count <= 262144 or not 0 <= seed <= 0xffffffff
            or not 5 <= data_bits <= 8):
        raise ValueError('invalid deterministic pattern request')
    result = bytearray()
    state = seed
    mask = (1 << data_bits) - 1
    for _ in range(count):
        state = (state * 1664525 + 1013904223) & 0xffffffff
        result.append((state >> 24) & mask)
    return bytes(result)


def fields(line):
    result = {}
    for item in line.split():
        if '=' in item:
            key, value = item.split('=', 1)
            result[key] = value
    return result


class PicoPeer:
    def __init__(self, path, timeout=10):
        self.path, self.timeout = path, timeout
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.buffer = bytearray()
        attributes = termios.tcgetattr(self.fd)
        attributes[0] = attributes[1] = attributes[3] = 0
        attributes[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attributes[4] = attributes[5] = termios.B115200
        termios.tcsetattr(self.fd, termios.TCSANOW, attributes)
        termios.tcflush(self.fd, termios.TCIOFLUSH)

    def close(self):
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        self.close()

    def _line(self, timeout=None):
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while True:
            marker = self.buffer.find(b'\n')
            if marker >= 0:
                raw = bytes(self.buffer[:marker]).rstrip(b'\r')
                del self.buffer[:marker + 1]
                try:
                    return raw.decode('ascii')
                except UnicodeError as exc:
                    raise RuntimeError(f'non-ASCII Pico response: {raw!r}') from exc
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RuntimeError('Pico USB response timeout')
            readable, _, _ = select.select([self.fd], [], [], remaining)
            if readable:
                try:
                    chunk = os.read(self.fd, 4096)
                except BlockingIOError:
                    continue
                if not chunk:
                    raise RuntimeError('Pico USB control disconnected')
                self.buffer.extend(chunk)

    def command(self, text, expected_error=None):
        payload = (text + '\n').encode('ascii')
        sent = 0
        deadline = time.monotonic() + self.timeout
        while sent < len(payload):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RuntimeError('Pico USB command write timeout')
            _, writable, _ = select.select([], [self.fd], [], remaining)
            if writable:
                try:
                    sent += os.write(self.fd, payload[sent:])
                except BlockingIOError:
                    pass
        line = self._line()
        if expected_error:
            expected = 'ERR ' + expected_error
            if line != expected:
                raise RuntimeError(
                    f'Pico command {text!r} expected {expected!r}, received {line!r}')
            return line
        if not line.startswith('OK'):
            raise RuntimeError(f'Pico command {text!r} failed: {line!r}')
        return line

    def event(self, name, timeout=15):
        expected = 'EVENT ' + name
        while True:
            line = self._line(timeout)
            if line.startswith(expected):
                return line
            if line.startswith('EVENT '):
                raise RuntimeError(f'unexpected Pico event: {line!r}')


def uart_config(baud, data_bits=8, parity='none', stop_bits=1):
    return dict(baud=baud, data_bits=data_bits, parity=parity, stop_bits=stop_bits,
                flow_control='none')


def send_serial(probe, data):
    for offset in range(0, len(data), 2048):
        probe.sock.sendall(frame(3, 0, b'\0\0\0\1' + data[offset:offset + 2048]))


def receive_serial(probe, count):
    received = bytearray()
    deadline = time.monotonic() + probe.timeout
    while len(received) < count:
        kind, request, size = decode_header(probe._exact(HEADER.size, deadline))
        payload = probe._exact(size, deadline)
        if kind != 3 or request or size <= 4 or payload[:4] != b'\0\0\0\1':
            raise RuntimeError('unexpected FMGO frame during Pico-to-Nano transfer')
        received.extend(payload[4:])
        if len(received) > count:
            raise RuntimeError('Nano returned extra Pico UART bytes')
    return bytes(received)


def require_peer_event(line, count):
    value = fields(line)
    if (value.get('overruns') != '0' or value.get('mismatches') != '0'
            or value.get('expect_remaining') != '0'
            or value.get('send_remaining') != '0'):
        raise RuntimeError(f'Pico reported transfer failure: {line!r}')
    return count


def verify_nano_configuration(args, options, boot_id, baud, actual_baud):
    with Probe(args.address, args.device_id, args.board, **options) as observer:
        if observer.hello.get('boot_id') != boot_id:
            raise RuntimeError('Nano rebooted while verifying UART configuration')
        validate_channel(observer.hello['channels'][0])
        config = observer.hello['channels'][0]['config']
        if config.get('baud') != baud or config.get('actual_baud') != actual_baud:
            raise RuntimeError(f'Nano UART configuration mismatch: {config!r}')
        observer.finish()


def run(args):
    options = dict(port=args.network_port, firmware_version=FIRMWARE_VERSION,
                   backend='uart', timeout=args.timeout)
    with PicoPeer(args.peer_port, args.timeout) as peer:
        identity = peer.command('PING')
        if fields(identity).get('peer') != PEER_VERSION:
            raise RuntimeError(f'unexpected Pico identity: {identity!r}')
        with Probe(args.address, args.device_id, args.board, **options) as baseline_probe:
            validate_channel(baseline_probe.hello['channels'][0])
            baseline = baseline_probe.command('device.diagnostics')
            baseline_probe.finish()

        peer.command('BAUD 115200')
        with Probe(args.address, args.device_id, args.board, **options) as nano:
            validate_channel(nano.hello['channels'][0])
            nano.command('serial.open', channel_id=1, config=uart_config(115200))
            verify_nano_configuration(args, options, nano.hello['boot_id'], 115200, 115207)
            peer.command('CLEAR')
            peer.command('MODE SINK')
            print('Verified peers:', nano.hello['firmware_version'], identity)

            nano_to_pico = pattern(args.byte_count, args.seed)
            peer.command(f'EXPECT {len(nano_to_pico)} {args.seed}')
            send_serial(nano, nano_to_pico)
            event = peer.event('expect_done', args.timeout)
            require_peer_event(event, len(nano_to_pico))
            print(f'Nano TX -> Pico RX: {len(nano_to_pico)} exact bytes')

            reverse_seed = args.seed ^ 0xffffffff
            expected = pattern(args.byte_count, reverse_seed)
            peer.command(f'SEND {len(expected)} {reverse_seed}')
            received = receive_serial(nano, len(expected))
            event = peer.event('send_done', args.timeout)
            require_peer_event(event, len(expected))
            if received != expected:
                offset = next(i for i, pair in enumerate(zip(received, expected))
                              if pair[0] != pair[1])
                raise RuntimeError(f'Pico-to-Nano mismatch at offset {offset}')
            print(f'Pico TX -> Nano RX: {len(received)} exact bytes')
            status_115200 = nano.command('serial.status', channel_id=1)
            if (status_115200.get('backend_rx_bytes') != args.byte_count
                    or status_115200.get('backend_tx_bytes') != args.byte_count
                    or status_115200.get('pending_to_backend') != 0
                    or status_115200.get('pending_to_peer') != 0):
                raise RuntimeError(f'unexpected Nano 115200 counters: {status_115200!r}')
            nano.command('serial.close', channel_id=1)
            nano.finish()

        # Reinitializing either UART can create a line-transition byte at the
        # other receiver. Change the peer while no Nano owner exists, then let
        # Nano acquisition discard it and clear the peer after Nano starts.
        peer.command('BAUD 57600')
        with Probe(args.address, args.device_id, args.board, **options) as nano:
            validate_channel(nano.hello['channels'][0])
            nano.command('serial.open', channel_id=1, config=uart_config(57600))
            verify_nano_configuration(args, options, nano.hello['boot_id'], 57600, 57597)
            peer.command('CLEAR')
            peer.command('MODE SINK')
            low_count = args.byte_count // 2
            low_seed = args.seed ^ 0x5a5a5a5a
            peer.command(f'EXPECT {low_count} {low_seed}')
            send_serial(nano, pattern(low_count, low_seed))
            require_peer_event(peer.event('expect_done', args.timeout), low_count)
            print(f'Nano TX -> Pico RX at 57600: {low_count} exact bytes')

            reverse_low_seed = low_seed ^ 0xffffffff
            expected = pattern(low_count, reverse_low_seed)
            peer.command(f'SEND {low_count} {reverse_low_seed}')
            received = receive_serial(nano, low_count)
            require_peer_event(peer.event('send_done', args.timeout), low_count)
            if received != expected:
                offset = next(i for i, pair in enumerate(zip(received, expected))
                              if pair[0] != pair[1])
                raise RuntimeError(f'57600 Pico-to-Nano mismatch at offset {offset}')
            print(f'Pico TX -> Nano RX at 57600: {low_count} exact bytes')

            status = nano.command('serial.status', channel_id=1)
            if (status.get('backend_rx_bytes') != low_count
                    or status.get('backend_tx_bytes') != low_count
                    or status.get('pending_to_backend') != 0
                    or status.get('pending_to_peer') != 0):
                raise RuntimeError(f'unexpected Nano UART counters: {status!r}')
            diagnostics = nano.command('device.diagnostics')
            for name in ('uart_rx_overrun_events', 'uart_rx_lost_bytes_minimum',
                         'tcp_errors'):
                if diagnostics.get(name) != baseline.get(name):
                    raise RuntimeError(
                        f'Nano diagnostic {name} changed from {baseline.get(name)!r} '
                        f'to {diagnostics.get(name)!r}: {diagnostics!r}')
            if diagnostics.get('uart_rx_discarded', 0) < baseline.get('uart_rx_discarded', 0):
                raise RuntimeError(f'Nano diagnostic failure: {diagnostics!r}')
            peer_stats = peer.command('STATS')
            peer_values = fields(peer_stats)
            if (peer_values.get('rx') != str(low_count)
                    or peer_values.get('tx') != str(low_count)
                    or peer_values.get('overruns') != '0'
                    or peer_values.get('mismatches') != '0'):
                raise RuntimeError(f'unexpected Pico counters: {peer_stats!r}')
            nano.command('serial.close', channel_id=1)
            nano.finish()
        print('Independent UART peer checks passed:', status)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', required=True)
    parser.add_argument('--device-id', required=True)
    parser.add_argument('--board', required=True, choices=['nano_rp2040_connect'])
    parser.add_argument('--peer-port', required=True)
    parser.add_argument('--network-port', type=int, default=7420)
    parser.add_argument('--byte-count', type=int, default=8192)
    parser.add_argument('--seed', type=lambda value: int(value, 0), default=0x50554552)
    parser.add_argument('--timeout', type=float, default=15)
    args = parser.parse_args()
    if not 2 <= args.byte_count <= 131072 or args.byte_count % 2:
        parser.error('--byte-count must be even and in 2..131072')
    if not 0 <= args.seed <= 0xffffffff:
        parser.error('--seed must be an unsigned 32-bit value')
    return run(args)


if __name__ == '__main__':
    raise SystemExit(main())
