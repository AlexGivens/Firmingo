"""Validate the portable handoff archive; not an SDK implementation or hardware test."""

import hashlib
import json
from pathlib import Path
import struct
import zipfile

import pytest

from experiments.managed_sketch.export_sdk_contract import ROOT, export_contract


def test_export_contains_exact_versioned_vectors_and_metadata_without_firmware(tmp_path):
    first, second = tmp_path / "first.zip", tmp_path / "second.zip"
    report = export_contract(first)
    export_contract(second)
    assert first.read_bytes() == second.read_bytes()
    assert report["sha256"] == hashlib.sha256(first.read_bytes()).hexdigest()
    with zipfile.ZipFile(first) as archive:
        manifest = json.loads(archive.read("manifest.json"))
        assert manifest["protocol_major"] == 1
        assert manifest["fixtures_are_executable"] is False
        assert set(archive.namelist()) == set(manifest["files"]) | {"manifest.json"}
        for name, metadata in manifest["files"].items():
            assert not Path(name).is_absolute() and ".." not in Path(name).parts
            assert not name.endswith((".uf2", ".elf", ".fms"))
            data = archive.read(name)
            assert data == (ROOT / name).read_bytes()
            assert metadata == {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        prefix = "tests/fixtures/protocol-v1/managed-upload/"
        states = set()
        for name in archive.namelist():
            if not name.startswith(prefix) or not name.endswith("-response.hex"):
                continue
            frame = bytes.fromhex(archive.read(name).decode())
            magic, version, kind, flags, request, size = struct.unpack("!4sBBHII", frame[:16])
            assert (magic, version, kind, flags, request) == (b"FMGO", 1, 2, 0, 9)
            assert size == len(frame) - 16
            response = json.loads(frame[16:])
            if "state" in response.get("result", {}):
                states.add(response["result"]["state"])
        assert states == {"accepted", "verified", "committing", "installed",
                          "boot_confirmed", "aborted", "failed"}
        failed = bytes.fromhex(archive.read(prefix + "install-failed-response.hex").decode())
        result = json.loads(failed[16:])
        assert result["ok"] is True and result["result"]["state"] == "failed"
        assert result["result"]["slot_touched"] is True
        assert result["result"]["error"] == "install_failed"


def test_export_refuses_overwriting_existing_work(tmp_path):
    output = tmp_path / "existing.zip"
    output.write_bytes(b"preserved artifact")
    with pytest.raises(FileExistsError):
        export_contract(output)
    assert output.read_bytes() == b"preserved artifact"
