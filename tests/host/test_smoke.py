from contextlib import contextmanager
import socket
import struct
import threading
import time

import pytest
from tools import smoke


def record(sequence=0):
    return struct.pack("<4sIIHH", b"PICO", sequence, 12345, 256, 0) + bytes(i ^ (sequence & 255) for i in range(256))


@pytest.mark.parametrize("sequence", [0, 1, 255, 256, 0xffffffff])
def test_accepts_exact_legacy_record(sequence):
    smoke.verify_record(record(sequence), sequence)


@pytest.mark.parametrize("offset", [0, 4, 12, 14, 16, 271])
def test_rejects_corrupted_record(offset):
    data = bytearray(record())
    data[offset] ^= 1
    with pytest.raises(smoke.VerificationError):
        smoke.verify_record(data, 0)


def test_reports_first_mismatch_offset_expected_and_observed():
    with pytest.raises(smoke.VerificationError, match="byte 101: expected 0xff, observed 0x01"):
        smoke.compare_bytes(b"\x00\x01", b"\x00\xff", 100)


@pytest.mark.parametrize("address", ["example.com", "0.0.0.0", "255.255.255.255", "224.0.0.1", "::1"])
def test_requires_numeric_unicast_ipv4(address):
    with pytest.raises(ValueError):
        smoke.validate_target(address)


class ChunkSocket:
    def __init__(self, chunks):
        self.chunks = iter(chunks)
        self.timeouts = []

    def settimeout(self, value):
        self.timeouts.append(value)

    def recv(self, _size):
        return next(self.chunks, b"")


def test_receive_exact_handles_fragmentation_and_reports_eof():
    connection = ChunkSocket([b"a", b"bc"])
    assert smoke.receive_exact(connection, 3, time.monotonic() + 1) == b"abc"
    with pytest.raises(smoke.VerificationError, match="expected 4 bytes, received 3"):
        smoke.receive_exact(ChunkSocket([b"abc"]), 4, time.monotonic() + 1)


def test_total_deadline_expires_even_when_data_keeps_arriving(monkeypatch):
    ticks = iter([0.0, 0.5, 1.0])
    monkeypatch.setattr(smoke.time, "monotonic", lambda: next(ticks))
    with pytest.raises(TimeoutError, match="total test deadline"):
        smoke.receive_exact(ChunkSocket([b"a", b"b", b"c"]), 3, 0.75)


@contextmanager
def local_service(handler):
    """Real loopback sockets test the harness; these are NOT hardware evidence."""
    errors = []
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(3)

        def serve():
            try:
                with listener.accept()[0] as connection:
                    connection.settimeout(3)
                    handler(connection)
            except Exception as exc:
                errors.append(exc)

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        try:
            yield listener.getsockname()[1]
        finally:
            worker.join(4)
            assert not worker.is_alive(), "local harness test server did not finish"
            assert not errors, errors


def test_source_checker_accepts_fragmented_and_coalesced_tcp_records():
    def serve(connection):
        data = b"".join(record(i) for i in range(3))
        connection.sendall(data[:7])
        connection.sendall(data[7:])
    with local_service(serve) as port:
        assert smoke.source("127.0.0.1", records=3, port=port)["bytes"] == 816


def test_echo_checker_sends_and_reads_concurrently():
    def serve(connection):
        while True:
            data = connection.recv(173)
            if not data:
                return
            connection.sendall(data)
    with local_service(serve) as port:
        result = smoke.echo("127.0.0.1", port=port, slow_read=True)
        assert result["bytes"] == 65536
        assert result["bytes_per_second"] > 0


def test_echo_checker_rejects_trailing_duplicate():
    def serve(connection):
        data = connection.recv(1)
        connection.sendall(data + data)
    with local_service(serve) as port:
        with pytest.raises(smoke.VerificationError, match="unsolicited|extra byte"):
            smoke.echo("127.0.0.1", byte_count=1, port=port)


def test_identity_checker_requires_selected_board_and_exact_id():
    from tools.smoke import identity
    import json
    body=json.dumps({'board':'nano_rp2040_connect','device_id':'a1b2c3d4e5f60718'}).encode()
    def serve(connection):
        assert b'GET /diagnostics HTTP/1.1' in connection.recv(512)
        connection.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: '+str(len(body)).encode()+b'\r\n\r\n'+body)
    with local_service(serve) as port:
        assert identity('127.0.0.1','a1b2c3d4e5f60718',port=port)['board']=='nano_rp2040_connect'
    with local_service(serve) as port:
        with pytest.raises(smoke.VerificationError,match='identity mismatch'):
            identity('127.0.0.1','0011223344556677',port=port)


@pytest.mark.parametrize('response',[
    b'HTTP/1.1 404 Not Found\r\nContent-Length: 2\r\n\r\n{}',
    b'HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\n{}',
    b'HTTP/1.1 200 OK\r\nContent-Length: 2\r\nTransfer-Encoding: chunked\r\n\r\n{}',
    b'HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n[]',
    b'HTTP/1.1 200 OK\r\n\r\n'+b'x'*4097,
])
def test_identity_checker_rejects_invalid_or_unbounded_response(response):
    from tools.smoke import identity
    def serve(connection):
        connection.recv(512)
        connection.sendall(response)
    with local_service(serve) as port:
        with pytest.raises(smoke.VerificationError):
            identity('127.0.0.1','a1b2c3d4e5f60718',port=port)
