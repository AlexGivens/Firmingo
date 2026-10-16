#!/usr/bin/env python3
"""Export versioned FMS2 firmware contracts and exact target-2 resident facts; never flash."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
from experiments.managed_sketch.export_resident import (
    ELF_LIMIT, UF2_LIMIT, COMMIT, HASH, bounded_file, check_uf2, git_blob, sha, unique_object,
)
from experiments.managed_sketch.build_stateful_proof import resident_contract

RESOURCES = {
    'contract/abi.h': 'experiments/managed_sketch/abi.h',
    'contract/stateful_abi.h': 'experiments/managed_sketch/stateful_abi.h',
    'contract/stateful_image.h': 'experiments/managed_sketch/stateful_image.h',
    'contract/stateful_image.cpp': 'experiments/managed_sketch/stateful_image.cpp',
    'contract/image.h': 'experiments/managed_sketch/image.h',
    'contract/image.cpp': 'experiments/managed_sketch/image.cpp',
    'contract/facade_v2/Arduino.h': 'experiments/managed_sketch/facade_v2/Arduino.h',
    'contract/module_entry.cpp': 'experiments/managed_sketch/module_entry.cpp',
    'contract/stateful_module.ld': 'experiments/managed_sketch/stateful_module.ld',
    'contract/image-layout.json': 'experiments/managed_sketch/stateful_contract.json',
    'contract/LICENSE': 'LICENSE',
}
VECTOR_NAMES = '''abort-request aborted-response accepted-response begin-request boot-confirmed-response
chunk-response chunk commit-request committing-response disconnected-response finish-request hello-request
hello-response install-failed-response installed-response old-target-request status-request unauthorized-response
verified-response wrong-target-response'''.split()
RESOURCES.update({'vectors/managed-upload-v2/' + name + '.hex':
                  'tests/fixtures/protocol-v1/managed-upload-v2/' + name + '.hex' for name in VECTOR_NAMES})
RESOURCES['vectors/managed-upload-v2/README.md'] = 'tests/fixtures/protocol-v1/managed-upload-v2/README.md'
RESOURCES['vectors/stateful-image/initialized.hex'] = 'tests/fixtures/managed-stateful-v2/initialized.hex'
RESOURCES['vectors/stateful-image/provenance.json'] = 'tests/fixtures/managed-stateful-v2/provenance.json'
REQUIRED = {
    'boards/nano_rp2040_connect/baseline.json', 'boards/nano_rp2040_connect/core-lock.json',
    'patches/arduino-pico-6.0.0-usb-startup/manifest.json',
    'patches/arduino-pico-6.0.0-usb-startup/deferred-ncm.patch', 'src/core/version.h',
    'experiments/managed_sketch/build_resident.py',
    'experiments/managed_sketch/firmingo_managed_resident/firmingo_managed_resident.ino',
    *['experiments/managed_sketch/' + name for name in (
        'abi.h', 'image.h', 'image.cpp', 'stateful_abi.h', 'stateful_image.h', 'stateful_image.cpp',
        'stateful_runtime.h', 'stateful_runtime.cpp', 'stateful_upload.h', 'stateful_upload.cpp',
        'core1_pause.h', 'core1_pause.cpp')],
}
VALIDATORS = ('experiments/managed_sketch/export_stateful.py',
              'experiments/managed_sketch/build_stateful_proof.py',
              'experiments/managed_sketch/export_resident.py',
              'experiments/managed_sketch/build_composite.py')


def validate_layout(layout, facts):
    """Reject an incompatible static resource even if its surrounding hashes agree."""
    if type(layout.get('schema')) is not int or type(layout.get('contractVersion')) is not int:
        raise ValueError('contract schema/version must be integers')
    if (layout.get('schema'), layout.get('identifier'), layout.get('contractVersion'),
        layout.get('board'), layout.get('profile'), layout.get('residentABI'), layout.get('apiTableBytes')) != (
            1, 'firmingo-nano-managed-abi2-v1-experimental', 1, 'nano_rp2040_connect', 'nano-managed-v2', 2, 52):
        raise ValueError('unsupported FMS2 contract identity')
    image = layout['image']
    if layout.get('architecture') != dict(family='armv6-m', cpu='cortex-m0plus', instructionSet='thumb',
                                           endianness='little', pointerBytes=4, floatABI='soft'):
        raise ValueError('unsupported Nano target architecture')
    expected = dict(magic='FMS2', version=2, headerBytes=128, codeOffset=256, boardTag=0x4f4e414e,
                    slotAddress=0x10200000, slotBytes=4194304, maximumCapsuleBytes=4096, sectionAlignment=4)
    if any(image.get(k) != v for k, v in expected.items()):
        raise ValueError('unsupported FMS2 image layout')
    names = ['boardTag', 'apiAddress', 'codeAddress', 'codeBytes', 'setupAddress', 'loopAddress',
             'dataAddress', 'dataBytes', 'bssAddress', 'bssBytes', 'dataLoadOffset', 'imageBytes', 'flags', 'reserved']
    fields = [('magic', 0, 4, 'ascii'), ('version', 4, 2, 'uint-le'), ('headerBytes', 6, 2, 'uint-le')]
    fields += [(name, 8 + i * 4, 4, 'zero' if name in ('flags', 'reserved') else 'uint-le') for i, name in enumerate(names)]
    fields += [('payloadSHA256', 64, 32, 'sha256'), ('headerSHA256', 96, 32, 'sha256'), ('padding', 128, 128, 'zero')]
    if image.get('fields') != [dict(name=n, offset=o, bytes=b, kind=k) for n, o, b, k in fields]:
        raise ValueError('unsupported FMS2 field offsets')
    if image.get('hashSpans') != {'payloadSHA256': {'start': 256, 'endField': 'imageBytes'}, 'headerSHA256': {'start': 0, 'end': 96}}:
        raise ValueError('unsupported FMS2 hash spans')
    if image.get('layout') != dict(codeAddress='slotAddress + codeOffset', dataLoadOffset='codeOffset + codeBytes',
                                  imageBytes='dataLoadOffset + dataBytes', dataAddress='resident stateAddress',
                                  bssAddress='dataAddress + dataBytes',
                                  stateBound='dataBytes + bssBytes <= resident stateCapacityBytes',
                                  entryRule='Thumb entry with at least two bytes in code extent',
                                  inputRule='exact image span; reject truncation and trailing bytes'):
        raise ValueError('unsupported FMS2 section constraints')
    if layout.get('initialization') != dict(copyData=True, clearBSS=True, clearUnusedArena=True,
                                           beforeSetup=True, repeatEachGeneration=True, nonvolatile=False,
                                           constructors=False, heap=False, sandbox=False, rollback=False):
        raise ValueError('unsupported initialization contract')
    if layout.get('buildDefinitions') != dict(cpp=['FIRMINGO_API_ADDRESS'], linker=['FIRMINGO_STATE_ADDRESS', 'FIRMINGO_STATE_BYTES']):
        raise ValueError('unsupported compiler/linker definitions')
    api = layout.get('api', {})
    signatures = [('pin_mode','void(uint32,uint32)'), ('digital_write','void(uint32,uint32)'),
                  ('clock_ms','uint32()'), ('delay_ms','void(uint32)'),
                  ('console_write','uint32(const uint8*,uint32)'), ('console_write_available','uint32()'),
                  ('console_read','int32()'), ('console_available','uint32()')]
    if api != dict(magic=0x49504146, version=2, bytes=52, pointerBytes=4, endianness='little',
                   functions=[dict(name=n, offset=8+4*i, signature=s) for i,(n,s) in enumerate(signatures)],
                   stateFields=dict(stateAddress=40, stateCapacityBytes=44, maximumCapsuleBytes=48)):
        raise ValueError('unsupported FAPI two language-neutral layout')
    upload = layout['upload']; actual = facts['uploadTarget']
    for key, value in dict(protocolMajor=1, targetID=actual['id'], format=actual['format'],
                           maximumCapsuleBytes=actual['max_size'], maximumChunkBytes=actual['max_chunk'],
                           authorization=actual['authorization'], stagingTimeoutMs=actual['stagingTimeoutMs'],
                           stagingTimeoutError='io_error', pauseTimeoutMs=1000).items():
        if upload.get(key) != value:
            raise ValueError('contract/upload ELF mismatch: ' + key)


def export_stateful(resident, output, commit, root=ROOT):
    if not COMMIT.fullmatch(commit):
        raise ValueError('select an exact lowercase firmware commit')
    resident, root = Path(resident), Path(root)
    report_bytes = bounded_file(resident / 'report.json', 262144)
    report = json.loads(report_bytes, object_pairs_hook=unique_object)
    target = dict(id=2, format='nano-managed-v2', max_size=4096, max_chunk=1024, authorization='physical-d2-gnd')
    if (not isinstance(report, dict) or report.get('status') != 'compile-tested FMS2 upload candidate; not flashed'
            or report.get('board') != 'nano_rp2040_connect' or report.get('core') != 'rp2040:rp2040@6.0.0'
            or report.get('stateful_runtime_enabled') is not True or report.get('network_uploads_enabled') is not True
            or report.get('upload_target') != target):
        raise ValueError('select the explicit FMS2 upload build, not boot-only/allocation/ABI one')
    sources = report.get('source_sha256')
    if not isinstance(sources, dict) or not REQUIRED <= sources.keys() or len(sources) > 256:
        raise ValueError('incomplete FMS2 resident source inventory')
    tracked = subprocess.run(['git', '--no-replace-objects', '-C', str(root), 'ls-tree', '-r', '--name-only', commit, '--', 'src'],
                             capture_output=True, text=True, check=True, timeout=15).stdout.splitlines()
    if any(name not in sources for name in tracked if name.endswith(('.h', '.hpp', '.cpp', '.c', '.inc', '.s', '.S'))):
        raise ValueError('resident inventory omits committed library inputs')
    for name, expected in sources.items():
        if not isinstance(expected, str) or not HASH.fullmatch(expected):
            raise ValueError('invalid resident source digest')
        if name == 'src/.DS_Store':
            continue  # Historical Finder metadata, excluded from exported inputs.
        if sha(git_blob(root, commit, name)) != expected:
            raise ValueError('resident source differs from selected commit: ' + name)
    baseline = json.loads(git_blob(root, commit, 'boards/nano_rp2040_connect/baseline.json'))
    if report.get('fqbn') != baseline['fqbn']:
        raise ValueError('resident FQBN differs from committed Nano pin')
    for field, budget in (('flash_bytes', 1048576), ('static_ram_bytes', 196608)):
        if type(report.get(field)) is not int or not 0 < report[field] <= budget or report.get(field.replace('bytes', 'budget_bytes')) != budget:
            raise ValueError('resident memory budget mismatch')
    elf = bounded_file(resident / 'artifacts/firmingo_managed_resident.ino.elf', ELF_LIMIT)
    uf2 = bounded_file(resident / 'artifacts/firmingo_managed_resident.ino.uf2', UF2_LIMIT)
    if sha(elf) != report.get('elf_sha256') or sha(uf2) != report.get('uf2_sha256'):
        raise ValueError('resident artifact/report hash mismatch')
    facts, flash = resident_contract(elf)
    if facts.get('flashCheckpoint') is not None:
        raise ValueError('flash checkpoint fault images cannot be exported as compiler residents')
    if not facts['runtimeEnabled'] or not facts['fms2NetworkUploadsEnabled']:
        raise ValueError('ELF lacks FMS2 runtime/upload contract')
    if (facts['linkerBounds']['__end__'] - 0x20000000 != report['static_ram_bytes']
            or any(address + len(data) > 0x10100000 for address, data in flash)):
        raise ValueError('actual ELF memory extents disagree with budget/report')
    compared = check_uf2(uf2, flash)
    version_source = git_blob(root, commit, 'src/core/version.h')
    match = re.search(rb'^#define FIRMINGO_VERSION "([0-9.]+)"$', version_source, re.MULTILINE)
    if match is None:
        raise ValueError('missing committed firmware version')
    version = match[1].decode() + '-statefulupload-exp1'
    if not any(version.encode() + b'\0' in data for _, data in flash):
        raise ValueError('ELF lacks selected firmware identity')
    resources = {name: git_blob(root, commit, source) for name, source in RESOURCES.items()}
    layout = json.loads(resources['contract/image-layout.json'], object_pairs_hook=unique_object)
    validate_layout(layout, facts)
    compact = lambda data: re.sub(rb'\s+|//[^\n]*', b'', data)
    expected_abi = b'FirmingoSketchApiV1 calls;uint8_t* state_address;uint32_t state_capacity_bytes;uint32_t maximum_capsule_bytes;'
    body = re.search(rb'struct FirmingoSketchApiV2\s*\{([^}]+)\};', resources['contract/stateful_abi.h'])
    if body is None or compact(body[1]) != compact(expected_abi):
        raise ValueError('unsupported FAPI two source layout')
    calls = re.search(rb'struct FirmingoSketchApiV1\s*\{([^}]+)\};', resources['contract/abi.h'])
    expected_calls = b'''uint32_t magic; uint16_t abi_version; uint16_t table_bytes;
      void (*pin_mode)(uint32_t pin, uint32_t mode); void (*digital_write)(uint32_t pin, uint32_t value);
      uint32_t (*clock_ms)(); void (*delay_ms)(uint32_t milliseconds);
      uint32_t (*console_write)(const uint8_t* data, uint32_t size);
      uint32_t (*console_write_available)(); int32_t (*console_read)(); uint32_t (*console_available)();'''
    if calls is None or compact(calls[1]) != compact(expected_calls):
        raise ValueError('unsupported FAPI call source signatures')
    layout['firmwareCommit'] = commit
    layout['resources'] = [dict(path=name, sourcePath=RESOURCES[name], bytes=len(data), sha256=sha(data)) for name, data in resources.items()]
    contract_bytes = (json.dumps(layout, sort_keys=True, indent=2) + '\n').encode()
    validator_hashes = {name: sha(git_blob(root, commit, name)) for name in VALIDATORS}
    for name, expected in validator_hashes.items():
        if sha((ROOT / name).read_bytes()) != expected:
            raise ValueError('use the exporter/validators from the selected firmware commit')
    facts.update(purpose='experimental FMS2 resident compiler input', firmwareCommit=commit, firmwareVersion=version,
                 profile='nano-managed-v2', residentUF2SHA256=sha(uf2), buildReportSHA256=sha(report_bytes),
                 compilerContractSHA256=sha(contract_bytes), elfUF2ComparedBytes=compared,
                 staticRAMBytes=report['static_ram_bytes'], flashBytes=report['flash_bytes'],
                 sourceSHA256={k:v for k,v in sources.items() if k != 'src/.DS_Store'}, validatorSHA256=validator_hashes,
                 qualification='experimental; hardware not qualified by export',
                 provenanceLimit='Committed project sources checked; installed core/toolchain pins declared, not a complete binary supply-chain inventory')
    files = {'resident.elf': elf, 'resident.uf2': uf2, 'build-report.json': report_bytes,
             'resident-facts.json': (json.dumps(facts, sort_keys=True, indent=2)+'\n').encode(),
             'contract.json': contract_bytes, **resources}
    manifest = dict(schema=3, purpose='experimental_stateful_resident_compiler_export', board='nano_rp2040_connect',
                    profile='nano-managed-v2', firmwareCommit=commit,
                    files=[dict(path=n, bytes=len(d), sha256=sha(d)) for n,d in files.items()],
                    hardwareQualified=False)
    manifest_bytes = (json.dumps(manifest, sort_keys=True, indent=2)+'\n').encode()
    output = Path(output); output.mkdir()  # No output until every check passes; existing paths refused.
    for name, data in {**files, 'manifest.json': manifest_bytes}.items():
        path = output / name; path.parent.mkdir(parents=True, exist_ok=True)
        with path.open('xb') as destination: destination.write(data)
    return dict(status='exported_experimental_stateful_resident', firmwareCommit=commit,
                manifestSHA256=sha(manifest_bytes), files=len(files), hardwareQualified=False)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--resident-dir', required=True, type=Path)
    p.add_argument('--firmware-commit', required=True)
    p.add_argument('--output', required=True, type=Path, help='new directory only')
    args = p.parse_args()
    try: print(json.dumps(export_stateful(args.resident_dir, args.output, args.firmware_commit)))
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error: p.exit(1, str(error)+'\n')


if __name__ == '__main__': main()
