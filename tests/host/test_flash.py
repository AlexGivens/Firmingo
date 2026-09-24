"""Check the flash gates using ordinary temporary files, never actual hardware."""
import argparse
import hashlib
import json
import subprocess
import sys
import pytest
from tools import dev


@pytest.fixture(params=['local-only', 'application'])
def prepared(tmp_path, monkeypatch, request):
    monkeypatch.setattr(dev, 'ROOT', tmp_path)
    mount = tmp_path / 'bootloader'
    mount.mkdir()
    (mount / 'INFO_UF2.TXT').write_text('UF2 Bootloader\nBoard-ID: RPI-RP2\n')
    firmware = request.param
    output = tmp_path / 'build/firmware/nano_rp2040_connect' / firmware
    (output / 'artifacts').mkdir(parents=True)
    source = tmp_path / 'source.c'
    source.write_text('original')
    sketch = 'firmingo_application' if firmware == 'application' else 'firmingo_local_only'
    image = output / 'artifacts' / (sketch + '.ino.uf2')
    image.write_bytes(b'test fixture; not firmware')
    report = {'status': 'compile-tested', 'firmware': firmware,
              'source_sha256': {'source.c': dev.digest(source)},
              'artifacts': {image.name: {'sha256': dev.digest(image)}}}
    (output / 'report.json').write_text(json.dumps(report))
    args = argparse.Namespace(board='nano_rp2040_connect', mount=str(mount), firmware=firmware)
    return args, mount, source, image, output, report


@pytest.mark.parametrize('fault', ['wrong_board', 'stale_source', 'bad_image', 'failed_build', 'wrong_profile'])
def test_invalid_flash_inputs_never_write_to_target(prepared, fault):
    args, mount, source, image, output, report = prepared
    if fault == 'wrong_board':
        (mount / 'INFO_UF2.TXT').write_text('Board-ID: another-board\n')
    elif fault == 'stale_source':
        source.write_text('changed')
    elif fault == 'bad_image':
        image.write_bytes(b'corrupt')
    elif fault == 'wrong_profile':
        report['firmware'] = 'application' if args.firmware == 'local-only' else 'local-only'
        (output / 'report.json').write_text(json.dumps(report))
    else:
        report['status'] = 'failed'
        (output / 'report.json').write_text(json.dumps(report))
    with pytest.raises(RuntimeError):
        dev.flash(args)
    assert not (mount / 'FIRMINGO.UF2').exists()


def test_explicit_valid_target_receives_only_verified_file(prepared):
    args, mount, source, image, output, report = prepared
    assert dev.flash(args) == 0
    assert (mount / 'FIRMINGO.UF2').read_bytes() == image.read_bytes()


def test_second_board_reference_sketch_does_not_require_nano_baseline_metadata(tmp_path, monkeypatch):
    monkeypatch.setattr(dev, 'ROOT', tmp_path)
    manifest = {'board': 'raspberry_pi_pico'}
    assert dev.selected_sketch(manifest, 'application') == tmp_path / 'examples/firmingo_application'
    with pytest.raises(RuntimeError, match='no preserved baseline'):
        dev.selected_sketch(manifest, 'baseline')


def test_hardware_harness_accepts_declared_second_board():
    result = subprocess.run(
        [sys.executable, '-m', 'pytest', 'tests/hardware', '--collect-only', '-q',
         '--board', 'raspberry_pi_pico', '--address', '192.0.2.1',
         '--device-id', '0000000000000000'],
        cwd=dev.ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        timeout=30,
    )
    assert result.returncode == 0, result.stdout
