"""The destructive stall probe must have a verified ROM recovery path first."""

import sys

import pytest

from experiments.managed_sketch import probe_stall


def test_missing_recovery_image_prevents_any_board_contact(monkeypatch):
    hash64 = "a" * 64
    monkeypatch.setattr(sys, "argv", ["probe_stall.py", "--address", "192.168.77.1",
                                  "--device-id", "0123456789abcdef",
                                  "--expected-resident-uf2-sha256", hash64,
                                  "--expected-stall-capsule-sha256", hash64,
                                  "--expected-blink-b-capsule-sha256", hash64,
                                  "--expected-recovery-uf2-sha256", hash64,
                                  "--picotool", "/nonexistent/picotool",
                                  "--run"])
    monkeypatch.setattr(probe_stall.managed_upload, "verified_capsule",
                        lambda *_args: b"capsule")

    def reject_recovery(_expected):
        raise RuntimeError("recovery image missing")

    def board_contact(*_args):
        pytest.fail("probe contacted the board before recovery preflight")

    monkeypatch.setattr(probe_stall, "checked_recovery_image", reject_recovery)
    monkeypatch.setattr(probe_stall.managed_upload, "selected_hello", board_contact)
    with pytest.raises(RuntimeError, match="recovery image missing"):
        probe_stall.main()
