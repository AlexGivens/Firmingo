"""Exercise managed build admission and actual artifact checks with non-executable fixtures."""

import argparse
import json
import shutil

import pytest

from tools import dev
from tools.managed_profile import build_managed, verify_outputs
from tools import usb_core
from test_resident_export import resident  # Reuse the independent ELF/UF2/source fixture.


def manifest(report):
    return {"board": report["board"], "fqbn": report["fqbn"],
            "flash_budget_bytes": 1048576, "static_ram_budget_bytes": 196608}


def test_checks_actual_load_bytes_and_reports_experimental_abi(resident):
    root, build, _, report = resident
    result = verify_outputs(root, build, manifest(report))
    assert result["api_address"] == "0x10000080"
    assert result["resident_abi"] == 1 and result["elf_uf2_compared_bytes"] == 256
    assert "qualification" not in result  # Artifact validity does not qualify hardware.


@pytest.mark.parametrize("fault", ["uf2", "api", "source", "omitted-source", "path", "budget", "status", "target"])
def test_corrupt_or_mismatched_builds_reject_even_with_self_consistent_hashes(resident, fault):
    root, build, _, report = resident
    if fault in ("uf2", "api"):
        extension = "uf2" if fault == "uf2" else "elf"
        path = build / "artifacts" / ("firmingo_managed_resident.ino." + extension)
        data = bytearray(path.read_bytes())
        data[32 if fault == "uf2" else 384] ^= 1
        path.write_bytes(data)
        report[extension + "_sha256"] = dev.digest(path)
    elif fault == "source":
        (root / "src/core/version.h").write_text("changed during compilation")
    elif fault == "omitted-source":
        (root / "src/core/unreported.h").write_text("unreported library build input")
    elif fault == "path":
        report["source_sha256"]["../escape"] = "0" * 64
    elif fault == "budget":
        report["static_ram_bytes"] = True
    elif fault == "target":
        report["upload_target"]["max_size"] = 8192
    else:
        report["status"] = "failed"
    (build / "report.json").write_text(json.dumps(report))
    with pytest.raises(ValueError):
        verify_outputs(root, build, manifest(report))


def setup_profile(root, monkeypatch):
    """Own every generated path; callbacks stand in for tools, never for ELF/UF2 validation."""
    monkeypatch.setattr(dev, "ROOT", root)
    for name in ("tools/dev.py", "tools/managed_profile.py", "tools/usb_core.py",
                 "boards/nano_rp2040_connect/core-lock.json",
                 "patches/arduino-pico-6.0.0-usb-startup/manifest.json"):
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("owned tooling fixture; NEVER DEPLOY")
    return argparse.Namespace(board="nano_rp2040_connect", subnet_octet=77, verbose=False)


@pytest.mark.parametrize("failure", ["toolchain", "missing-input"])
def test_setup_failure_invalidates_earlier_success_before_any_compiler(resident, monkeypatch, failure):
    root, build, _, report = resident
    args = setup_profile(root, monkeypatch)
    output = root / "build/firmware/nano_rp2040_connect/managed"
    shutil.copytree(build, output / "resident")
    (output / "profile-report.json").write_text('{"status":"compile-tested"}')

    def fail(_):
        raise RuntimeError("missing pinned CLI")
    monkeypatch.setattr(dev, "check_arduino", fail)
    if failure == "missing-input":
        (root / "boards/nano_rp2040_connect/core-lock.json").unlink()
    with pytest.raises((RuntimeError, FileNotFoundError)):
        build_managed(args, manifest(report))
    saved = json.loads((output / "profile-report.json").read_text())
    assert saved["status"] == "failed" and not saved["production_ready"]
    assert not (output / "resident/report.json").exists()


@pytest.mark.parametrize("changed", [False, True])
def test_success_requires_verified_outputs_and_unchanged_profile_inputs(resident, monkeypatch, changed):
    root, build, _, report = resident
    args = setup_profile(root, monkeypatch)
    output = root / "build/firmware/nano_rp2040_connect/managed"
    monkeypatch.setattr(dev, "check_arduino", lambda _: ["fixture-cli"])
    monkeypatch.setattr(dev, "effective_arduino_config", lambda _: {})
    monkeypatch.setattr(usb_core, "prepare", lambda *a: (["fixture-cli", "--config-file", "fixture.yaml"], {}))

    def compile_fixture(command, timeout):
        assert command[-2] == "--output-dir" and timeout == 660
        shutil.copytree(build, output / "resident")
        if changed:
            (root / "tools/usb_core.py").write_text("changed during compilation")
    monkeypatch.setattr(dev, "run", compile_fixture)
    if changed:
        with pytest.raises(ValueError, match="profile input changed"):
            build_managed(args, manifest(report))
    else:
        assert build_managed(args, manifest(report)) == 0
    saved = json.loads((output / "profile-report.json").read_text())
    assert saved["status"] == ("failed" if changed else "compile-tested")
    assert saved["qualification"] == "experimental"
    assert saved["hardware_tested"] is False and saved["production_ready"] is False


def test_pico_is_rejected_before_toolchain_or_output_creation(tmp_path):
    args = argparse.Namespace(board="raspberry_pi_pico", subnet_octet=77, verbose=False)
    with pytest.raises(ValueError, match="only for nano"):
        build_managed(args, {})


def test_report_only_compile_cannot_reuse_previous_exported_images(resident, monkeypatch):
    root, build, _, report = resident
    args = setup_profile(root, monkeypatch)
    output = root / "build/firmware/nano_rp2040_connect/managed"
    shutil.copytree(build, output / "resident")
    monkeypatch.setattr(dev, "check_arduino", lambda _: ["fixture-cli"])
    monkeypatch.setattr(dev, "effective_arduino_config", lambda _: {})
    monkeypatch.setattr(usb_core, "prepare", lambda *a: (["fixture-cli", "--config-file", "fixture.yaml"], {}))
    monkeypatch.setattr(dev, "run", lambda *a, **k: (output / "resident/report.json").write_text(json.dumps(report)))
    with pytest.raises(FileNotFoundError):
        build_managed(args, manifest(report))
    saved = json.loads((output / "profile-report.json").read_text())
    assert saved["status"] == "failed"
    assert not (output / "resident/artifacts").exists()


@pytest.mark.parametrize("option,value", [("subnet_octet", 78), ("verbose", True)])
def test_unsupported_overrides_are_explicit(option, value):
    args = argparse.Namespace(board="nano_rp2040_connect", subnet_octet=77, verbose=False)
    setattr(args, option, value)
    with pytest.raises(ValueError, match="omit subnet/verbose overrides"):
        build_managed(args, {})
