#!/usr/bin/env python3
"""Explicit native FMS2 fault-test stage/commit; no reset, reconnect, retry or ROM write."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
from experiments.managed_sketch.build_stateful_proof import resident_contract
from experiments.managed_sketch.export_resident import bounded_file, check_uf2, unique_object
from experiments.managed_sketch.fmgo_upload import snapshot
from tools.session_smoke import Probe
from tools.version import current_version

TARGET = dict(id=2, format='nano-managed-v2', max_size=4096, max_chunk=1024,
              authorization='physical-d2-gnd')


def checked_inputs(proof, resident_sha, capsule_sha, validator):
    """Bind real artifacts, actual mapped facts, recorded sources and an independent native checker."""
    load = lambda p: json.loads(bounded_file(p, 262144), object_pairs_hook=unique_object)
    report = load(proof / 'report.json')
    resident = load(proof / 'resident/report.json')
    sha = lambda b: hashlib.sha256(b).hexdigest()
    if (report.get('status') != 'native ARM link and host initialization passed; not executed on ARM'
            or resident.get('status') != 'compile-tested FMS2 flash-checkpoint fault candidate; not flashed'
            or sha((proof / 'resident/report.json').read_bytes()) != report.get('residentReportSHA256')):
        raise ValueError('select an exact completed flash-checkpoint native proof')
    for name, expected in {**report['sourceSHA256'], **resident['source_sha256']}.items():
        if sha(bounded_file(ROOT / name, 4 * 1024 * 1024)) != expected:
            raise ValueError('proof source changed: ' + name)
    elf = bounded_file(proof / 'resident/artifacts/firmingo_managed_resident.ino.elf', 8 * 1024 * 1024)
    uf2 = bounded_file(proof / 'resident/artifacts/firmingo_managed_resident.ino.uf2', 4 * 1024 * 1024)
    capsule = bounded_file(proof / 'stateful_counter.fms', 4096)
    if (sha(elf) != resident['elf_sha256'] or sha(uf2) != resident_sha or resident_sha != resident['uf2_sha256']
            or sha(capsule) != capsule_sha or capsule_sha != report['capsuleSHA256']):
        raise ValueError('reviewed artifacts differ from exact proof')
    contract, flash = resident_contract(elf)
    check_uf2(uf2, flash)
    # The proof adds only measured UF2 fields to the independently reconstructed facts.
    if ({k: v for k, v in report['contract'].items() if k not in ('residentUF2SHA256', 'elfUF2ComparedBytes')} != contract
            or contract.get('flashCheckpoint') != resident.get('flash_checkpoint')
            or not contract.get('flashCheckpoint')):
        raise ValueError('fault selection differs from real resident facts')
    if sha(bounded_file(validator, 8 * 1024 * 1024)) != report['validatorSHA256']:
        raise ValueError('native checker differs from proof')
    result = subprocess.run([str(validator.resolve()), str((proof / 'stateful_counter.fms').resolve()),
        str(contract['apiAddress']), str(contract['stateAddress']), str(contract['stateCapacityBytes'])],
        capture_output=True, text=True, timeout=15)
    if result.returncode:
        raise ValueError('independent production FMS2 validation failed')
    return capsule, contract


def transfer(probe, capsule, save):
    """Stage and record one commit intent durably; never reconcile/retry during the physical checkpoint."""
    digest = hashlib.sha256(capsule).hexdigest()
    with probe:
        base = dict(bootID=probe.hello['boot_id'], capsuleSHA256=digest, capsuleBytes=len(capsule),
                    state='staging', commitAttempts=0, automaticReplay=False)
        save(base)
        value = probe.command('flash.begin', target_id=2, board_id='nano_rp2040_connect',
                              format=TARGET['format'], size=len(capsule), sha256=digest)
        upload_id = value.get('upload_id')
        if type(upload_id) is not int or not 1 <= upload_id <= 0xffffffff:
            raise RuntimeError('invalid transaction ID')
        snapshot(value, upload_id, digest, len(capsule))
        if value['state'] != 'accepted' or value['received'] or value['slot_touched']:
            raise RuntimeError('begin did not acknowledge a new RAM stage')
        for offset in range(0, len(capsule), 1024):
            part = capsule[offset:offset + 1024]
            value = snapshot(probe.upload_chunk(upload_id, offset, part), upload_id, digest, len(capsule))
            if value['state'] != 'accepted' or value['received'] != offset + len(part) or value['slot_touched']:
                raise RuntimeError('chunk acknowledgement differs from selected stage')
        value = snapshot(probe.command('flash.finish', target_id=2, upload_id=upload_id), upload_id, digest, len(capsule))
        if value['state'] != 'verified' or value['slot_touched']:
            raise RuntimeError('RAM verification did not pass')
        base.update(state='commitIntent', uploadID=upload_id, commitAttempts=1)
        save(base)  # Failure here prevents the only commit send.
        try:
            value = snapshot(probe.command('flash.commit', target_id=2, upload_id=upload_id), upload_id, digest, len(capsule))
            if value['state'] != 'committing':
                raise RuntimeError('unexpected commit reply; preserve uncertainty')
            base.update(state='committing', reply=value)
        except (OSError, RuntimeError) as error:
            base.update(state='outcomeUnknown', cause=str(error)[:512])
        save(base)
        return base


def main():
    """Require explicit board/device/artifacts and a new journal before opening one socket."""
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--board', choices=('nano_rp2040_connect',), required=True)
    p.add_argument('--address', required=True)
    p.add_argument('--device-id', required=True)
    p.add_argument('--proof-dir', type=Path, required=True)
    p.add_argument('--expected-resident-sha256', required=True)
    p.add_argument('--expected-capsule-sha256', required=True)
    p.add_argument('--validator', type=Path, required=True)
    p.add_argument('--output-dir', type=Path, required=True)
    p.add_argument('--install-once', action='store_true', required=True,
                   help='authorize one managed-slot commit after physically arming D2-to-GND')
    a = p.parse_args()
    if (not re.fullmatch('[0-9a-f]{16}', a.device_id)
            or any(not re.fullmatch('[0-9a-f]{64}', value) for value in (a.expected_resident_sha256, a.expected_capsule_sha256))):
        p.error('select exact lowercase device ID and artifact SHA-256 values')
    capsule, contract = checked_inputs(a.proof_dir.resolve(), a.expected_resident_sha256, a.expected_capsule_sha256, a.validator)
    a.output_dir.mkdir(parents=True, exist_ok=False)
    count = 0
    def save(value):
        nonlocal count
        if count >= 8:
            raise RuntimeError('journal bound exceeded')
        with (a.output_dir / f'event-{count:02d}.local.json').open('x') as output:
            json.dump(value, output, indent=2); output.write('\n'); output.flush(); os.fsync(output.fileno())
        directory = os.open(a.output_dir, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
        count += 1
    phase = contract['flashCheckpoint']['after']
    version = current_version() + ('-statefulflash-erase-exp1' if phase == 'erase' else '-statefulflash-page-exp1')
    save(dict(kind='explicit native fault test', phase=phase, residentSHA256=a.expected_resident_sha256,
              capsuleSHA256=a.expected_capsule_sha256, chipBusyInterruption=False))
    result = transfer(Probe(a.address, a.device_id, board=a.board, firmware_version=version,
                            timeout=5, targets=[TARGET]), capsule, save)
    print(json.dumps(result, indent=2), flush=True)
    print('No automatic status query or retry. Observe D13 checkpoint pulses; preserve the record before ROM recovery.', flush=True)


if __name__ == '__main__':
    main()
