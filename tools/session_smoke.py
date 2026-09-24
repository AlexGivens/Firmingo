"""Small FMGO application-firmware validator; no flashing or host network edits."""
import json
import re
import random
import select
import socket
import struct
import time
from tools.smoke import validate_target
from tools.version import current_version

HEADER = struct.Struct('!4sBBHII')
MAX_PAYLOAD = 4096
FIRMWARE_VERSION = current_version()


def frame(kind, request, payload):
    if len(payload) > MAX_PAYLOAD:
        raise RuntimeError('test frame exceeds device payload limit')
    return HEADER.pack(b'FMGO', 1, kind, 0, request, len(payload)) + payload


def decode_header(header):
    magic, version, kind, flags, request, size = HEADER.unpack(header)
    if (magic != b'FMGO' or version != 1 or flags or size > MAX_PAYLOAD
            or kind not in (2, 3) or (kind == 2) != (request != 0)):
        raise RuntimeError(f'invalid firmware frame header: {header.hex()}')
    return kind, request, size


class Probe:
    def __init__(self, address, device_id, board, port=7420, timeout=10,
                 firmware_version=FIRMWARE_VERSION, backend='application'):
        address = validate_target(address)
        if not re.fullmatch('[0-9a-fA-F]{16}', device_id):
            raise RuntimeError('expected device ID must be exactly 16 hexadecimal digits')
        device_id = device_id.lower()
        self.sock = socket.create_connection((address, port), timeout=timeout)
        self.timeout, self.next_request = timeout, 1
        try:
            self.hello = self.command('hello', max_payload=MAX_PAYLOAD)
            value = self.hello
            if (value.get('device_id') != device_id or value.get('board_id') != board
                    or value.get('firmware_version') != firmware_version
                    or value.get('protocol_major') != 1
                    or not re.fullmatch('[0-9a-f]{16}', str(value.get('boot_id')))
                    or type(value.get('max_payload')) is not int
                    or value['max_payload'] != MAX_PAYLOAD
                    or value.get('auth_mode') != 'open-development'
                    or value.get('targets') != []
                    or not isinstance(value.get('channels'), list)
                    or len(value['channels']) != 1
                    or not isinstance(value['channels'][0], dict)
                    or value['channels'][0].get('id') != 1
                    or value['channels'][0].get('backend') != backend
                    or value['channels'][0].get('controls') != []):
                raise RuntimeError(f'firmware identity/capabilities mismatch: {value!r}')
        except Exception:
            self.close()
            raise

    def close(self):
        self.sock.close()

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        self.close()

    def finish(self):
        """Observe remote TCP closure, not merely release the local descriptor.

        No pending requests/data are allowed. Firmware EOF is an abort, so RST
        is an expected close event. A new connect is never retried after refusal.
        """
        if self.sock.fileno() < 0:
            return
        try:
            self.sock.shutdown(socket.SHUT_WR)
            self.sock.settimeout(min(self.timeout, 3))
            try:
                trailing = self.sock.recv(1)
            except ConnectionResetError:
                trailing = b''
            except socket.timeout as exc:
                raise RuntimeError('peer TCP closure was not observed before deadline') from exc
            if trailing:
                raise RuntimeError('unexpected trailing application data at TCP close')
        finally:
            self.close()

    def _exact(self, count, deadline):
        data = bytearray()
        while len(data) < count:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RuntimeError(f'FMGO timeout: expected {count} bytes, received {len(data)}')
            self.sock.settimeout(remaining)
            chunk = self.sock.recv(count - len(data))
            if not chunk:
                raise RuntimeError(f'FMGO disconnected: expected {count} bytes, received {len(data)}')
            data.extend(chunk)
        return bytes(data)

    def command(self, op, expected_error=None, **fields):
        request = self.next_request
        self.next_request += 1
        payload = json.dumps(dict(op=op, **fields), separators=(',', ':')).encode()
        deadline = time.monotonic() + self.timeout
        self.sock.settimeout(self.timeout)
        self.sock.sendall(frame(1, request, payload))
        kind, actual_request, count = decode_header(self._exact(HEADER.size, deadline))
        if kind != 2 or actual_request != request:
            raise RuntimeError(f'expected response ID {request}, received type {kind}, ID {actual_request}')
        try:
            response = json.loads(self._exact(count, deadline))
        except (ValueError, UnicodeError) as exc:
            raise RuntimeError('firmware response is not valid JSON') from exc
        if not isinstance(response, dict):
            raise RuntimeError(f'expected JSON response object: {response!r}')
        if expected_error:
            if (response.get('ok') is not False or not isinstance(response.get('error'), dict)
                    or response['error'].get('code') != expected_error):
                raise RuntimeError(f'expected {expected_error}, received {response!r}')
            return response['error']
        if response.get('ok') is not True or not isinstance(response.get('result'), dict):
            raise RuntimeError(f'command {op} failed: {response!r}')
        return response['result']

    def echo(self, byte_count=16384, slow_read=False, seed=None, read_delay=0.15):
        if not 1 <= byte_count <= 262144:
            raise RuntimeError('echo size must be 1..262144 bytes')
        if seed is not None and (type(seed) is not int or not 0 <= seed <= 0xffffffff):
            raise RuntimeError('echo seed must be an unsigned 32-bit integer')
        if not isinstance(read_delay, (int, float)) or not 0 <= read_delay <= 10:
            raise RuntimeError('read delay must be 0..10 seconds')
        expected = (random.Random(seed).randbytes(byte_count) if seed is not None else
                    bytes(((i * 73) ^ (i >> 8)) & 255 for i in range(byte_count)))
        wire = b''.join(frame(3, 0, b'\0\0\0\1' + expected[i:i+2048])
                        for i in range(0, len(expected), 2048))
        sent, turn = 0, 0
        incoming, received = bytearray(), bytearray()
        started = time.monotonic()
        deadline = started + self.timeout
        fragments = (1, 7, 19, 128, 511)
        self.sock.setblocking(False)
        try:
            while sent < len(wire) or len(received) < len(expected):
                now = time.monotonic()
                remaining = deadline - now
                if remaining <= 0:
                    raise RuntimeError(f'echo timeout: expected {byte_count} bytes, received {len(received)}, sent wire {sent}/{len(wire)}')
                read_now = not slow_read or now - started >= read_delay
                readable, writable, _ = select.select([self.sock] if read_now else [],
                                                       [self.sock] if sent < len(wire) else [], [], min(remaining, 0.05))
                if writable:
                    try:
                        sent += self.sock.send(wire[sent:sent+fragments[turn % len(fragments)]])
                        turn += 1
                    except BlockingIOError:
                        pass
                    except OSError as exc:
                        raise RuntimeError(f'echo socket send failed: expected {byte_count} bytes, '
                                           f'received {len(received)}, sent wire {sent}/{len(wire)}: {exc!r}') from exc
                if readable:
                    try:
                        chunk = self.sock.recv(4096)
                    except BlockingIOError:
                        continue
                    except OSError as exc:
                        raise RuntimeError(f'echo socket receive failed: expected {byte_count} bytes, '
                                           f'received {len(received)}, sent wire {sent}/{len(wire)}: {exc!r}') from exc
                    if not chunk:
                        raise RuntimeError(f'echo disconnected after {len(received)}/{byte_count} bytes')
                    incoming.extend(chunk)
                    while len(incoming) >= HEADER.size:
                        kind, request, size = decode_header(incoming[:HEADER.size])
                        if len(incoming) < HEADER.size + size:
                            break
                        payload = bytes(incoming[HEADER.size:HEADER.size+size])
                        del incoming[:HEADER.size+size]
                        if kind != 3 or request or size <= 4 or payload[:4] != b'\0\0\0\1':
                            raise RuntimeError('unexpected frame/channel during binary echo')
                        received.extend(payload[4:])
                        if len(received) > len(expected):
                            raise RuntimeError('firmware returned extra application bytes')
            if incoming:
                raise RuntimeError('unexpected trailing partial frame after echo')
            if received != expected:
                offset = next(i for i, (a, b) in enumerate(zip(expected, received)) if a != b)
                raise RuntimeError(f'echo first mismatch at byte {offset}: expected {expected[offset]:02x}, received {received[offset]:02x}, seed={seed}')
            elapsed = time.monotonic() - started
            return {'bytes': len(received), 'seconds': elapsed, 'bytes_per_second': len(received)/elapsed,
                    'fragmented_sends': turn, 'slow_read': slow_read, 'read_delay': read_delay, 'seed': seed}
        finally:
            self.sock.settimeout(self.timeout)


def smoke(address, device_id, board, port=7420):
    with Probe(address, device_id, board, port) as owner:
        print('Verified FMGO device:', owner.hello)
        owner.command('serial.open', channel_id=1)
        with Probe(address, device_id, board, port) as other:
            if other.hello['boot_id'] != owner.hello['boot_id']:
                raise RuntimeError('boot ID changed between concurrent sessions')
            other.command('serial.open', expected_error='busy', channel_id=1)
            print('Second-client BUSY: pass')
            first = owner.echo()
            print('Exact fragmented full-duplex echo:', first)
            second = owner.echo(4096, slow_read=True)
            print('Exact echo with delayed reader:', second)
            status = owner.command('serial.status', channel_id=1)
            if status.get('backend_rx_bytes') != 20480 or status.get('backend_tx_bytes') != 20480:
                raise RuntimeError(f'unexpected backend byte counters: {status!r}')
            print('Backend counters:', status)
            owner.command('device.reset', expected_error='unsupported', scope='device')
            print('Unsupported reset: pass (no reset performed)')
            owner.command('serial.close', channel_id=1)
            other.command('serial.open', channel_id=1)
            print('Ownership transfer echo:', other.echo(4096))
            other.command('serial.close', channel_id=1)
            other.finish()
        owner.finish()
    print('Both peer TCP closures observed; connecting fresh session')
    with Probe(address, device_id, board, port) as reconnected:
        reconnected.command('serial.open', channel_id=1)
        print('Fresh-session reconnect echo:', reconnected.echo(4096))
        reconnected.command('serial.close', channel_id=1)
        reconnected.finish()
    return 0
