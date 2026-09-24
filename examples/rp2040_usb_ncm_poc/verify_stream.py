#!/usr/bin/env python3
"""Validate the deterministic byte stream from TCP port 5000."""

import socket
import struct
import sys

HOST = sys.argv[1] if len(sys.argv) > 1 else "192.168.7.1"
PORT = 5000
RECORD_SIZE = 272


def receive_exact(connection: socket.socket, size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        chunk = connection.recv(size - len(chunks))
        if not chunk:
            raise ConnectionError("stream ended")
        chunks.extend(chunk)
    return bytes(chunks)


with socket.create_connection((HOST, PORT), timeout=5) as connection:
    connection.settimeout(5)
    for expected_sequence in range(100):
        record = receive_exact(connection, RECORD_SIZE)
        magic, sequence, device_ms, payload_size, flags = struct.unpack(
            "<4sIIHH", record[:16]
        )
        payload = record[16:]

        assert magic == b"PICO", magic
        assert sequence == expected_sequence, (sequence, expected_sequence)
        assert payload_size == 256, payload_size
        assert flags == 0, flags
        expected_payload = bytes(i ^ (sequence & 0xFF) for i in range(256))
        assert payload == expected_payload, f"bad payload in record {sequence}"

        if sequence % 10 == 0:
            print(f"validated record {sequence:3d}, device uptime {device_ms} ms")

print("PASS: validated 100 records / 27,200 deterministic bytes")
