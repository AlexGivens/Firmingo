"""Offline ROM composition against production native validation; fixtures must never be flashed."""
import struct
import subprocess
import sys
from pathlib import Path

import pytest

from experiments.managed_sketch import compose_stateful_initial as initial

ROOT = Path(__file__).resolve().parents[2]
API, STATE, CAPACITY = 0x10000100, 0x20030000, 32


@pytest.fixture(scope="module")
def validator(tmp_path_factory):
    """Build the real checker without relying on another checkout or an existing build."""
    output = tmp_path_factory.mktemp("stateful-composer-native")
    subprocess.run(["cmake", "-S", str(ROOT), "-B", str(output)], check=True, capture_output=True, timeout=30)
    subprocess.run(["cmake", "--build", str(output), "--target", "managed_stateful_check", "--parallel", "2"],
                   check=True, capture_output=True, timeout=60)
    return output / "managed_stateful_check"


def resident():
    """Synthetic UF2 carrying a real-shaped FAPI2, exclusively for host validation."""
    blocks = []
    for index in range(2):
        block = bytearray(512)
        struct.pack_into("<8I", block, 0, *initial.initial.uf2.MAGIC_START, 0x2000,
                         0x10000000 + index * 256, 256, index, 2, initial.initial.uf2.FAMILY)
        struct.pack_into("<I", block, 508, initial.initial.uf2.MAGIC_END)
        if index == 1:
            struct.pack_into("<IHH11I", block, 32, 0x49504146, 2, 52, *([0x10000041] * 8), STATE, CAPACITY, 4096)
        blocks.append(block)
    return b"".join(blocks)


def capsule():
    """Independent immutable FMS2 golden, containing non-executable test bytes."""
    return bytes.fromhex((ROOT / "tests/fixtures/managed-stateful-v2/initialized.hex").read_text())


def test_exact_resident_and_entire_managed_sector_preserved(validator):
    image, result = initial.compose(resident(), capsule(), API, STATE, CAPACITY, validator)
    assert "ARM NOT executed" in result
    blocks = [image[i:i + 512] for i in range(0, len(image), 512)]
    assert len(blocks) == 18
    for index, block in enumerate(blocks):
        assert struct.unpack_from("<II", block, 20) == (index, 18)
    assert b"".join(b[32:288] for b in blocks[:2]) == b"".join(resident()[i + 32:i + 288] for i in (0, 512))
    assert [struct.unpack_from("<I", b, 12)[0] for b in blocks[2:]] == list(range(0x10200000, 0x10201000, 256))
    assert b"".join(b[32:288] for b in blocks[2:]) == capsule() + b"\xff" * (4096 - len(capsule()))


def test_wrong_resident_facts_and_native_capsule_rejection_prevent_composition(validator):
    for api, state, capacity in [(API + 4, STATE, CAPACITY), (API, STATE + 4, CAPACITY), (API, STATE, CAPACITY + 4)]:
        with pytest.raises(ValueError, match="resident"):
            initial.compose(resident(), capsule(), api, state, capacity, validator)
    corrupt = bytearray(capsule()); corrupt[-1] ^= 1
    with pytest.raises(ValueError, match="checker rejected"):
        initial.compose(resident(), corrupt, API, STATE, CAPACITY, validator)
    with pytest.raises(ValueError, match="sector limit"):
        initial.compose(resident(), bytes(4097), API, STATE, CAPACITY, validator)
    bad = bytearray(resident()); struct.pack_into("<I", bad, 12, 0x10200000)
    with pytest.raises(ValueError, match="reservation"):
        initial.compose(bad, capsule(), API, STATE, CAPACITY, validator)


def test_isolated_cli_rejects_wrong_pin_and_never_overwrites_output(validator, tmp_path):
    r, c, output = tmp_path / "resident.uf2", tmp_path / "capsule.fms", tmp_path / "prepared"
    r.write_bytes(resident()); c.write_bytes(capsule())
    command = [sys.executable, "-I", "-S", "-B", initial.__file__, "--resident-uf2", str(r),
               "--expected-resident-sha256", initial.initial.uf2.sha(resident()), "--capsule", str(c),
               "--expected-capsule-sha256", initial.initial.uf2.sha(capsule()), "--expected-api-address", str(API),
               "--expected-state-address", str(STATE), "--expected-state-capacity", str(CAPACITY),
               "--validator", str(validator), "--output-dir", str(output)]
    bad = command.copy(); bad[bad.index("--expected-capsule-sha256") + 1] = "0" * 64
    assert subprocess.run(bad, capture_output=True, timeout=20).returncode != 0
    assert not output.exists()
    assert subprocess.run(command, capture_output=True, timeout=20).returncode == 0
    exact = (output / "initial.uf2").read_bytes()
    assert subprocess.run(command, capture_output=True, timeout=20).returncode != 0
    assert (output / "initial.uf2").read_bytes() == exact
