"""Private M7 host sender tests; these do not establish Nano hardware behavior."""

import hashlib
import socket

import pytest

from experiments.managed_sketch import upload as managed_upload


class FakeConnection:
    def __init__(self, response):
        self.response = response
        self.writes = bytearray()
        self.chunks = []
        self.half_closed = False

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        pass

    def settimeout(self, _seconds):
        pass

    def sendall(self, chunk):
        self.chunks.append(len(chunk))
        self.writes.extend(chunk)

    def shutdown(self, how):
        assert how == socket.SHUT_WR
        self.half_closed = True

    def recv(self, count):
        assert count == 1 and self.half_closed
        return self.response


def test_private_sender_frames_digest_id_and_half_closes_before_response(monkeypatch):
    connection = FakeConnection(b"K")
    monkeypatch.setattr(managed_upload.socket, "create_connection",
                        lambda address, timeout: connection)
    capsule = bytes(range(256)) + b"\x00\xff\r\n"
    device_id = "5031503337360009"
    managed_upload.upload("192.168.77.1", device_id, capsule, 10)
    frame = bytes(connection.writes)
    assert connection.half_closed
    assert max(connection.chunks) <= 128
    assert frame[:8] == b"FMSU\x01\x00\x00\x00"
    assert int.from_bytes(frame[8:12], "little") == len(capsule)
    assert frame[12:44] == hashlib.sha256(capsule).digest()
    assert frame[44:60] == device_id.encode()
    assert frame[60:] == capsule + b"GO!!"


def test_private_sender_reports_denial_as_failure(monkeypatch):
    connection = FakeConnection(b"D")
    monkeypatch.setattr(managed_upload.socket, "create_connection",
                        lambda address, timeout: connection)
    with pytest.raises(RuntimeError, match="physical arm absent"):
        managed_upload.upload("192.168.77.1", "5031503337360009", b"abc", 10)


def test_private_sender_can_assert_expected_timeout_for_pause_probe(monkeypatch):
    connection = FakeConnection(b"T")
    monkeypatch.setattr(managed_upload.socket, "create_connection",
                        lambda address, timeout: connection)
    assert managed_upload.upload("192.168.77.1", "5031503337360009",
                                 b"abc", 10, expected_response=b"T") == b"T"
