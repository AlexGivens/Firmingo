"""The experimental ROM loader must reject the wrong board before any write."""

import subprocess

import pytest

from experiments.managed_sketch import flash_initial


def pinned_tool(tmp_path):
    tool = tmp_path / "pqt-picotool" / "5.0.0-9576866" / "picotool"
    tool.parent.mkdir(parents=True)
    tool.write_bytes(b"fake tool")
    return tool


def test_rom_loader_rejects_wrong_flash_id_before_load(monkeypatch, tmp_path):
    tool = pinned_tool(tmp_path)
    calls = []

    def fake_run(command, **_kwargs):
        calls.append(command)
        return subprocess.CompletedProcess(command, 0,
            stdout="Device Information\n flash id: 0xaaaaaaaaaaaaaaaa\n")

    monkeypatch.setattr(flash_initial.subprocess, "run", fake_run)
    with pytest.raises(RuntimeError, match="flash ID"):
        flash_initial.install_with_picotool(
            tool, "5031503337360009", tmp_path / "image.uf2")
    assert [command[1] for command in calls] == ["info"]


def test_rom_loader_verifies_before_reboot(monkeypatch, tmp_path):
    tool = pinned_tool(tmp_path)
    image = tmp_path / "image.uf2"
    image.write_bytes(b"test image")
    calls = []

    def fake_run(command, **_kwargs):
        calls.append(command)
        return subprocess.CompletedProcess(command, 0,
            stdout="Device Information\n flash id: 0x5031503337360009\n")

    monkeypatch.setattr(flash_initial.subprocess, "run", fake_run)
    flash_initial.install_with_picotool(tool, "5031503337360009", image)
    assert [command[1] for command in calls] == ["info", "load", "verify", "reboot"]
    assert calls[1][2:] == ["-v", str(image), "-t", "uf2"]
    assert calls[2][2:] == [str(image), "-t", "uf2"]
