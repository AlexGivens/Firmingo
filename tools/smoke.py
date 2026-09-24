"""Nondestructive legacy-prototype checks with exact bytes and total deadlines."""

import ipaddress
import json
import re
import selectors
import socket
import struct
import time
from contextlib import contextmanager


class VerificationError(RuntimeError):
    pass


def validate_target(address):
    # Numeric addressing avoids DNS/rebinding ambiguity in a hardware test.
    target = ipaddress.IPv4Address(address)
    if target.is_multicast or target.is_unspecified or str(target) == "255.255.255.255":
        raise ValueError("select one unicast board IPv4 address")
    return str(target)


def remaining(deadline):
    seconds = deadline - time.monotonic()
    if seconds <= 0:
        raise TimeoutError("total test deadline expired")
    return seconds


@contextmanager
def connect(address, port, deadline):
    with socket.create_connection((address, port), timeout=remaining(deadline)) as connection:
        yield connection


def receive_exact(connection, size, deadline):
    data = bytearray()
    while len(data) < size:
        try:
            connection.settimeout(remaining(deadline))
            chunk = connection.recv(size - len(data))
        except TimeoutError as exc:
            raise TimeoutError(f"{exc}; expected {size} bytes, received {len(data)}") from exc
        if not chunk:
            raise VerificationError(f"EOF: expected {size} bytes, received {len(data)}")
        data.extend(chunk)
    return bytes(data)


def compare_bytes(actual, expected, offset=0):
    for index, (observed, wanted) in enumerate(zip(actual, expected)):
        if observed != wanted:
            raise VerificationError(
                f"first mismatch at byte {offset + index}: "
                f"expected 0x{wanted:02x}, observed 0x{observed:02x}"
            )
    if len(actual) != len(expected):
        raise VerificationError(
            f"length mismatch at byte {offset + min(len(actual), len(expected))}: "
            f"expected {len(expected)} bytes, observed {len(actual)}"
        )


def verify_record(record, sequence):
    if len(record) != 272:
        raise VerificationError(f"record {sequence}: expected 272 bytes, observed {len(record)}")
    magic, observed, _uptime, size, flags = struct.unpack("<4sIIHH", record[:16])
    expected = (b"PICO", sequence & 0xffffffff, 256, 0)
    actual = (magic, observed, size, flags)
    if actual != expected:
        raise VerificationError(f"record {sequence}: expected header {expected!r}, observed {actual!r}")
    compare_bytes(record[16:], bytes(i ^ (sequence & 255) for i in range(256)), sequence * 272 + 16)


def identity(address, device_id, board="nano_rp2040_connect", timeout=5, port=80):
    """Verify the selected Nano via its bounded qualification diagnostic page.

    Separate from the draft public protocol. Numeric target, no DNS/redirects,
    fixed response limit and one total deadline even if the peer drips bytes.
    """
    address = validate_target(address)
    if not re.fullmatch(r"[0-9a-fA-F]{16}", device_id):
        raise ValueError("device ID must be exactly 16 hexadecimal digits")
    deadline = time.monotonic() + timeout
    request = f"GET /diagnostics HTTP/1.1\r\nHost: {address}\r\nConnection: close\r\n\r\n".encode("ascii")
    try:
        with connect(address, port, deadline) as connection:
            connection.settimeout(remaining(deadline))
            connection.sendall(request)
            response = bytearray()
            while True:
                connection.settimeout(remaining(deadline))
                chunk = connection.recv(min(512, 4097 - len(response)))
                if not chunk:
                    break
                response.extend(chunk)
                if len(response) > 4096:
                    raise VerificationError("identity response exceeds 4096-byte limit")
        headers, body = bytes(response).split(b"\r\n\r\n", 1)
        if not headers.split(b"\r\n", 1)[0].startswith(b"HTTP/1.1 200 "):
            raise VerificationError("identity endpoint did not return HTTP 200")
        if len(body) > 2048:
            raise VerificationError("identity body exceeds 2048-byte limit")
        fields = {}
        for line in headers.split(b"\r\n")[1:]:
            name, value = line.split(b":", 1)
            name = name.strip().lower()
            if name in fields:
                raise VerificationError("duplicate identity HTTP header")
            fields[name] = value.strip()
        if b"transfer-encoding" in fields or int(fields.get(b"content-length", b"-1")) != len(body):
            raise VerificationError("identity response requires an exact Content-Length")
        reported = json.loads(body)
        if not isinstance(reported, dict):
            raise VerificationError("identity response is not an object")
        observed = reported.get("device_id", "")
        if not isinstance(observed, str) or observed.lower() != device_id.lower() or reported.get("board") != board:
            raise VerificationError(f"identity mismatch: expected {board}/{device_id.lower()}, observed {reported.get('board')}/{observed}")
        return reported
    except (TimeoutError, OSError, ValueError) as exc:
        raise VerificationError(f"identity verification failed: {exc}") from exc


def source(address, records=20, timeout=5, port=5000):
    start = time.monotonic()
    deadline = start + timeout
    sequence = 0
    try:
        with connect(address, port, deadline) as connection:
            for sequence in range(records):
                verify_record(receive_exact(connection, 272, deadline), sequence)
    except (TimeoutError, OSError, VerificationError) as exc:
        raise VerificationError(f"source record {sequence}, {sequence * 272} verified bytes: {exc}") from exc
    return report("source", records * 272, start)


def echo(address, byte_count=65536, timeout=10, port=5001, slow_read=False):
    # Deterministic data includes every byte; no random dependency.
    payload = bytes(((i * 73) ^ (i >> 8) ^ (i >> 16)) & 255 for i in range(byte_count))
    start = time.monotonic()
    deadline = start + timeout
    sent = received = 0
    fragments = (1, 7, 255, 256, 1023)
    fragment = 0
    try:
        with connect(address, port, deadline) as connection, selectors.DefaultSelector() as selector:
            connection.setblocking(False)
            selector.register(connection, selectors.EVENT_READ | selectors.EVENT_WRITE)
            read_after = time.monotonic() + (0.15 if slow_read else 0)
            while received < byte_count:
                events = selectors.EVENT_READ if sent == byte_count else selectors.EVENT_READ | selectors.EVENT_WRITE
                # Temporarily stop reading to put pressure on device TX queues.
                if time.monotonic() < read_after:
                    if sent == byte_count:
                        time.sleep(min(0.01, remaining(deadline)))
                        continue
                    events = selectors.EVENT_WRITE
                selector.modify(connection, events)
                for _key, ready in selector.select(min(0.1, remaining(deadline))):
                    if ready & selectors.EVENT_WRITE and sent < byte_count:
                        size = fragments[fragment % len(fragments)]
                        try:
                            count = connection.send(payload[sent:sent + size])
                        except BlockingIOError:
                            count = 0
                        sent += count
                        fragment += 1
                    if ready & selectors.EVENT_READ:
                        try:
                            data = connection.recv(4096)
                        except BlockingIOError:
                            continue
                        if not data:
                            raise VerificationError(f"echo EOF after {received}/{byte_count} bytes")
                        if received + len(data) > sent:
                            raise VerificationError(f"unsolicited echo data at byte {received}")
                        compare_bytes(data, payload[received:received + len(data)], received)
                        received += len(data)
            # Detect trailing duplicates already queued (bounded observation).
            connection.settimeout(min(0.1, remaining(deadline)))
            try:
                extra = connection.recv(1)
            except socket.timeout:
                extra = b""
            if extra:
                raise VerificationError(f"unexpected extra byte at offset {byte_count}")
    except (TimeoutError, OSError) as exc:
        raise VerificationError(f"echo: sent {sent}/{byte_count}, received {received}/{byte_count}: {exc}") from exc
    return report("echo", received, start)


def report(name, byte_count, start):
    elapsed = time.monotonic() - start
    return {"test": name, "bytes": byte_count, "seconds": round(elapsed, 6),
            "bytes_per_second": round(byte_count / max(elapsed, 1e-9), 1)}
