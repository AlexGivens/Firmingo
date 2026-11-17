#!/usr/bin/env python3
"""Prepare a pinned FMS2 ROM composite using the production native capsule checker; never flash."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('initial_uf2', HERE / 'compose_initial.py')
initial = importlib.util.module_from_spec(spec)
spec.loader.exec_module(initial)


def compose(resident, capsule, api, state, capacity, validator):
    """Check the capsule and emit one full UF2 block group per touched physical sector."""
    blocks = initial.uf2.checked_resident_bytes(resident)
    memory = {struct.unpack_from('<I', b, 12)[0] + i: b[32 + i] for b in blocks for i in range(256)}
    try: table = bytes(memory[api + i] for i in range(52))
    except KeyError as error: raise ValueError('Selected resident API is absent from UF2') from error
    if struct.unpack_from('<IHH', table) != (0x49504146, 2, 52) or struct.unpack_from('<3I', table, 40) != (state, capacity, 4096):
        raise ValueError('Selected API/state facts do not match the actual resident')
    if not 260 <= len(capsule) <= 4096: raise ValueError('FMS2 capsule exceeds initial sector limit')
    with tempfile.TemporaryDirectory(prefix='firmingo-stateful-initial-') as tmp:
        snapshot = Path(tmp) / 'checked.fms'
        snapshot.write_bytes(capsule)
        check = subprocess.run([str(validator.resolve(strict=True)), str(snapshot), str(api), str(state), str(capacity)],
                               capture_output=True, text=True, timeout=15)
    if check.returncode or len(check.stdout) > 8192 or len(check.stderr) > 8192:
        raise ValueError('Production FMS2 checker rejected the selected capsule: ' + check.stderr[:512])
    # The RP2040 USB MSC ROM tracks erases by UF2 block number / 16. If a new
    # physical sector begins mid-group, a later group erases its earlier pages.
    erased = b'\xff' * initial.SECTOR_BYTES
    sectors = {initial.uf2.SLOT: bytearray(capsule + erased[len(capsule):])}
    for block in blocks:
        address = struct.unpack_from('<I', block, 12)[0]
        base = address & ~(initial.SECTOR_BYTES - 1)
        if base not in sectors:
            sectors[base] = bytearray(erased)
        offset = address - base
        sectors[base][offset:offset + 256] = block[32:288]

    image = []
    total = len(sectors) * (initial.SECTOR_BYTES // 256)
    for base, sector in sorted(sectors.items()):
        for offset in range(0, initial.SECTOR_BYTES, 256):
            block = bytearray(512)
            struct.pack_into('<8I', block, 0, *initial.uf2.MAGIC_START, 0x2000, base + offset,
                             256, len(image), total, initial.uf2.FAMILY)
            block[32:288] = sector[offset:offset + 256]
            struct.pack_into('<I', block, 508, initial.uf2.MAGIC_END)
            image.append(block)
    return b''.join(image), check.stdout.strip()


def main():
    """Require exact caller-selected input hashes, real resident facts and a new output directory."""
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--resident-uf2', type=Path, required=True)
    p.add_argument('--expected-resident-sha256', required=True)
    p.add_argument('--capsule', type=Path, required=True)
    p.add_argument('--expected-capsule-sha256', required=True)
    p.add_argument('--expected-api-address', type=lambda v: int(v, 0), required=True)
    p.add_argument('--expected-state-address', type=lambda v: int(v, 0), required=True)
    p.add_argument('--expected-state-capacity', type=int, required=True)
    p.add_argument('--validator', type=Path, required=True)
    p.add_argument('--output-dir', type=Path, required=True)
    a = p.parse_args()
    resident = initial.pinned_bytes(a.resident_uf2, a.expected_resident_sha256, 4 * 1024 * 1024)
    capsule = initial.pinned_bytes(a.capsule, a.expected_capsule_sha256, 4096)
    image, result = compose(resident, capsule, a.expected_api_address, a.expected_state_address, a.expected_state_capacity, a.validator)
    report = {'schema': 1, 'status': 'prepared FMS2 initial ROM image; not flashed or hardware-qualified',
              'board': 'nano_rp2040_connect', 'profile': 'nano-managed-v2', 'residentUF2SHA256': initial.uf2.sha(resident),
              'capsuleSHA256': initial.uf2.sha(capsule), 'capsuleBytes': len(capsule), 'compositeUF2SHA256': initial.uf2.sha(image),
              'compositeUF2Bytes': len(image), 'apiAddress': a.expected_api_address, 'stateAddress': a.expected_state_address,
              'romLayout': 'erase-sector-aligned',
              'residentPaddingBytes': (len(image) - len(resident)) // 2 - initial.SECTOR_BYTES,
              'stateCapacityBytes': a.expected_state_capacity, 'managedSectorAddress': initial.uf2.SLOT, 'managedSectorBytes': 4096,
              'validatorSHA256': hashlib.sha256(a.validator.read_bytes()).hexdigest(), 'nativeCheck': result,
              'effect': 'Replace touched resident sectors and initialize first managed sector; untouched sectors outside image',
              'hardwareQualified': False}
    a.output_dir.mkdir(parents=True, exist_ok=False)
    (a.output_dir / 'initial.uf2').write_bytes(image)
    (a.output_dir / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__': main()
