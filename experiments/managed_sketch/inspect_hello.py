#!/usr/bin/env python3
"""Read one bounded FMGO hello from an explicitly selected numeric address."""

import argparse
import ipaddress
import json
import socket
import struct
import time

HEADER = struct.Struct("!4sBBHII")


def exact(connection, count, deadline):
    data = bytearray()
    while len(data) < count:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(f"hello timed out after {len(data)}/{count} bytes")
        connection.settimeout(remaining)
        chunk = connection.recv(count - len(data))
        if not chunk:
            raise RuntimeError(f"hello closed after {len(data)}/{count} bytes")
        data.extend(chunk)
    return bytes(data)


def read_hello(address, timeout=5):
    address = ipaddress.IPv4Address(address)
    if address.is_multicast or address.is_unspecified or int(address) == 0xffffffff:
        raise ValueError("address must be a unicast IPv4 address")
    if not 1 <= timeout <= 30:
        raise ValueError("timeout must be 1..30 seconds")
    deadline = time.monotonic() + timeout
    payload = b'{"op":"hello"}'
    request = HEADER.pack(b"FMGO", 1, 1, 0, 1, len(payload)) + payload
    with socket.create_connection((str(address), 7420), timeout=timeout) as connection:
        connection.settimeout(max(0.001, deadline - time.monotonic()))
        connection.sendall(request)
        header = exact(connection, HEADER.size, deadline)
        magic, version, kind, flags, request_id, size = HEADER.unpack(header)
        if (magic, version, kind, flags, request_id) != (b"FMGO", 1, 2, 0, 1):
            raise RuntimeError("unexpected FMGO hello response header")
        if size > 4096:
            raise RuntimeError("hello response exceeds the protocol payload limit")
        response = json.loads(exact(connection, size, deadline))
    if (not isinstance(response, dict) or response.get("ok") is not True
            or not isinstance(response.get("result"), dict)):
        raise RuntimeError(f"hello failed: {response!r}")
    return response["result"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", required=True, help="selected numeric board IPv4 address")
    parser.add_argument("--timeout", type=float, default=5,
                        help="total deadline in seconds, 1..30 (default 5)")
    args = parser.parse_args()
    print(json.dumps(read_hello(args.address, args.timeout), indent=2, sort_keys=True))
    print("Identity is self-reported; compare device_id with the selected board's prior record.")


if __name__ == "__main__":
    main()
