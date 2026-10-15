"""Offline initial composition keeps exact resident bytes and bounds all writes."""

import hashlib
import struct
import subprocess
import sys

import pytest

from experiments.managed_sketch import build_composite as uf2
from experiments.managed_sketch import compose_initial as initial


def resident():
    block = bytearray(512)
    struct.pack_into("<8I", block, 0, *uf2.MAGIC_START, 0x2000,
                     0x10000000, 256, 0, 1, uf2.FAMILY)
    block[32:288] = bytes(range(256))
    struct.pack_into("<I", block, 508, uf2.MAGIC_END)
    return bytes(block)


def capsule(code_bytes=4):
    # Synthetic non-executable parser fixture; never used as a hardware image.
    code = bytes(code_bytes)
    data = bytearray(256) + code
    struct.pack_into("<4sHHIIIIII", data, 0, b"FMS1", 1, 64, 0x4f4e414e,
                     0x10022c9c, uf2.SLOT + 256, code_bytes,
                     uf2.SLOT + 257, uf2.SLOT + 257)
    data[32:64] = hashlib.sha256(code).digest()
    return bytes(data)


def test_composition_preserves_resident_and_initializes_only_first_slot_sector():
    image = initial.compose(resident(), capsule(), 0x10022c9c)
    blocks = [image[i:i + 512] for i in range(0, len(image), 512)]
    assert len(blocks) == 17
    assert blocks[0][32:288] == resident()[32:288]
    for index, block in enumerate(blocks):
        assert struct.unpack_from("<II", block, 20) == (index, 17)
    assert [struct.unpack_from("<I", block, 12)[0] for block in blocks[1:]] == list(
        range(uf2.SLOT, uf2.SLOT + 4096, 256))
    sector = b"".join(block[32:288] for block in blocks[1:])
    assert sector == capsule() + b"\xff" * (4096 - len(capsule()))


def test_wrong_binding_corrupt_capsule_and_oversized_capsule_rejected():
    with pytest.raises(ValueError, match="API address"):
        initial.compose(resident(), capsule(), 0x10022ca0)
    corrupt = bytearray(capsule())
    corrupt[-1] ^= 1
    with pytest.raises(ValueError, match="SHA-256"):
        initial.compose(resident(), corrupt, 0x10022c9c)
    with pytest.raises(ValueError, match="4096"):
        initial.compose(resident(), capsule(3841), 0x10022c9c)


def test_resident_slot_writes_rejected():
    bad = bytearray(resident())
    struct.pack_into("<I", bad, 12, uf2.SLOT)
    with pytest.raises(ValueError, match="reservation"):
        initial.compose(bad, capsule(), 0x10022c9c)


def test_independent_input_pins_and_bounded_read(tmp_path):
    path = tmp_path / "capsule.fms"
    path.write_bytes(capsule())
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    assert initial.pinned_bytes(path, digest, 4096) == capsule()
    with pytest.raises(ValueError, match="SHA-256"):
        initial.pinned_bytes(path, "0" * 64, 4096)
    with pytest.raises(ValueError, match="size"):
        initial.pinned_bytes(path, digest, 259)


def test_isolated_cli_never_overwrites_prepared_output(tmp_path):
    resident_path = tmp_path / "resident.uf2"
    capsule_path = tmp_path / "capsule.fms"
    resident_path.write_bytes(resident())
    capsule_path.write_bytes(capsule())
    output = tmp_path / "prepared"
    command = [sys.executable, "-I", "-S", "-B", initial.__file__,
               "--resident-uf2", str(resident_path),
               "--expected-resident-sha256", uf2.sha(resident()),
               "--capsule", str(capsule_path),
               "--expected-capsule-sha256", uf2.sha(capsule()),
               "--expected-api-address", "0x10022c9c", "--output-dir", str(output)]
    assert subprocess.run(command, capture_output=True, timeout=10).returncode == 0
    prepared = (output / "initial.uf2").read_bytes()
    assert subprocess.run(command, capture_output=True, timeout=10).returncode != 0
    assert (output / "initial.uf2").read_bytes() == prepared
