"""Loopback peers validate the host harness only, never count as board evidence."""
from contextlib import contextmanager
import json
import socket
import threading
import time
import pytest
from tools import session_smoke as fmgo

ID = 'a1b2c3d4e5f60718'
BOARD = 'nano_rp2040_connect'
HELLO = dict(device_id=ID, board_id=BOARD, firmware_version=fmgo.FIRMWARE_VERSION,
             boot_id='0123456789abcdef', protocol_major=1, max_payload=4096,
             auth_mode='open-development', targets=[],
             channels=[dict(id=1, backend='application', controls=[])])


def exact(peer, count):
    value = bytearray()
    while len(value) < count:
        part = peer.recv(count-len(value))
        if not part:
            return None
        value.extend(part)
    return bytes(value)


@contextmanager
def endpoint(fault=None):
    commands, errors = [], []
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0)); listener.listen(); listener.settimeout(3)

        def serve():
            try:
                with listener.accept()[0] as peer:
                    peer.settimeout(3)
                    while True:
                        header = exact(peer, fmgo.HEADER.size)
                        if header is None:
                            return
                        _, _, kind, _, request, count = fmgo.HEADER.unpack(header)
                        payload = exact(peer, count)
                        if kind == 1:
                            command = json.loads(payload); commands.append(command['op'])
                            result = dict(HELLO) if command['op'] == 'hello' else {}
                            if command['op'] == 'hello' and fault == 'identity':
                                result['device_id'] = '0000000000000000'
                            response = dict(ok=True, result=result)
                            if command['op'] == 'device.reset':
                                response = dict(ok=False, error=dict(code='unsupported'))
                            wire = fmgo.frame(2, request, json.dumps(response).encode())
                        else:
                            if fault == 'eof':
                                return
                            if fault == 'stall':
                                time.sleep(0.35); return
                            if fault == 'corrupt':
                                payload = payload[:4]+bytes([payload[4]^1])+payload[5:]
                            if fault == 'channel':
                                payload = b'\0\0\0\2'+payload[4:]
                            # Split/coalesce frames independently of the host's sends.
                            wire = fmgo.frame(3,0,payload[:4]+payload[4:9])
                            if len(payload)>9:
                                wire += fmgo.frame(3,0,payload[:4]+payload[9:])
                        peer.sendall(wire[:7]); peer.sendall(wire[7:])
            except (ConnectionResetError, BrokenPipeError):
                pass # Probe intentionally closes after rejected/corrupt output.
            except Exception as exc:
                errors.append(exc)

        worker = threading.Thread(target=serve, daemon=True); worker.start()
        try:
            yield listener.getsockname()[1], commands
        finally:
            worker.join(4)
            assert not worker.is_alive(), 'host fake peer did not finish'
            assert not errors, errors


def test_identity_is_checked_before_any_channel_command():
    with endpoint('identity') as (port, commands):
        with pytest.raises(RuntimeError, match='identity/capabilities mismatch'):
            fmgo.Probe('127.0.0.1', ID, BOARD, port)
        assert commands == ['hello']


def test_uppercase_selected_device_id_matches_lowercase_wire_identity(monkeypatch):
    device_id = 'b2c3d4e5f6071829'
    monkeypatch.setitem(HELLO, 'device_id', device_id)
    with endpoint() as (port, commands):
        with fmgo.Probe('127.0.0.1', device_id.upper(), BOARD, port):
            pass
        assert commands == ['hello']


def test_partial_frames_full_duplex_binary_and_unsupported_control():
    with endpoint() as (port, commands):
        with fmgo.Probe('127.0.0.1', ID, BOARD, port) as probe:
            probe.command('serial.open', channel_id=1)
            assert probe.echo(8192, slow_read=True)['bytes'] == 8192
            assert probe.echo(1024)['fragmented_sends'] > 1
            assert probe.command('device.reset', expected_error='unsupported', scope='device')['code'] == 'unsupported'
        assert commands == ['hello', 'serial.open', 'device.reset']


@pytest.mark.parametrize('fault,message', [('corrupt','first mismatch at byte 0'),
                                          ('channel','unexpected frame/channel'),
                                          ('eof','disconnected'), ('stall','timeout')])
def test_bad_binary_and_stalled_peer_cannot_be_reported_as_success(fault, message):
    with endpoint(fault) as (port, _commands):
        with fmgo.Probe('127.0.0.1', ID, BOARD, port, timeout=0.3) as probe:
            probe.command('serial.open', channel_id=1)
            with pytest.raises(RuntimeError, match=message):
                probe.echo(1024)


@pytest.mark.parametrize('header', [fmgo.HEADER.pack(b'XXXX',1,2,0,1,0),
                                   fmgo.HEADER.pack(b'FMGO',2,2,0,1,0),
                                   fmgo.HEADER.pack(b'FMGO',1,2,1,1,0),
                                   fmgo.HEADER.pack(b'FMGO',1,2,0,1,4097),
                                   fmgo.HEADER.pack(b'FMGO',1,3,0,1,4),
                                   fmgo.HEADER.pack(b'FMGO',1,2,0,0,4)])
def test_response_headers_reject_wire_errors_before_body(header):
    with pytest.raises(RuntimeError, match='invalid firmware frame header'):
        fmgo.decode_header(header)


def test_confirmed_close_consumes_peer_eof_before_reconnect():
    with endpoint() as (port, _commands):
        with fmgo.Probe('127.0.0.1', ID, BOARD, port) as probe:
            probe.finish()
            assert probe.sock.fileno() == -1


class ClosingSocket:
    def __init__(self, result):
        self.result, self.closed, self.shutdowns = result, False, []
    def fileno(self):
        return -1 if self.closed else 123
    def shutdown(self, mode):
        self.shutdowns.append(mode)
    def settimeout(self, _value):
        pass
    def recv(self, _count):
        if isinstance(self.result, Exception):
            raise self.result
        return self.result
    def close(self):
        self.closed = True


@pytest.mark.parametrize('result', [b'', ConnectionResetError()])
def test_peer_eof_or_reset_are_close_events_not_reconnect_retries(result):
    probe = object.__new__(fmgo.Probe)
    probe.sock, probe.timeout = ClosingSocket(result), 5
    probe.finish()
    assert probe.sock.closed
    assert probe.sock.shutdowns == [socket.SHUT_WR]
    probe.finish() # Already closed: no extra I/O.
    assert probe.sock.shutdowns == [socket.SHUT_WR]


@pytest.mark.parametrize('result,message', [(b'x','trailing application data'),
                                          (socket.timeout(),'closure was not observed')])
def test_unconfirmed_close_and_unread_data_cannot_pass(result, message):
    probe = object.__new__(fmgo.Probe)
    probe.sock, probe.timeout = ClosingSocket(result), 5
    with pytest.raises(RuntimeError, match=message):
        probe.finish()
    assert probe.sock.closed


def test_seeded_payloads_have_exact_integrity_and_vary_between_batches():
    with endpoint() as (port, _commands):
        with fmgo.Probe('127.0.0.1', ID, BOARD, port) as probe:
            probe.command('serial.open', channel_id=1)
            assert probe.echo(4096, seed=0x12345678)['seed']==0x12345678
            assert probe.echo(4096, seed=0x87654321)['bytes']==4096


def test_seeded_corruption_reports_offset_and_seed():
    with endpoint('corrupt') as (port, _commands):
        with fmgo.Probe('127.0.0.1', ID, BOARD, port) as probe:
            with pytest.raises(RuntimeError,match='first mismatch at byte 0.*seed=1234'):
                probe.echo(1024,seed=1234)
