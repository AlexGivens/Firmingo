#!/usr/bin/env python3
"""Opt-in exact-capsule FMGO install/status harness; never flashes in smoke."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from experiments.managed_sketch.upload import verified_capsule
from tools.session_smoke import Probe
from tools.version import current_version

TARGET = dict(id=1, format='nano-managed-v1', max_size=4096, max_chunk=1024,
              authorization='physical-d2-gnd')


def selected_probe(address, device_id, timeout=10):
    return Probe(address=address, device_id=device_id, board='nano_rp2040_connect',
                 firmware_version=current_version() + '-m9exp1', timeout=timeout,
                 targets=[TARGET])


def snapshot(value, upload_id, digest, size):
    if (not isinstance(value, dict) or type(value.get('upload_id')) is not int
            or value.get('upload_id') != upload_id or value.get('sha256') != digest
            or type(value.get('size')) is not int or value.get('size') != size
            or type(value.get('received')) is not int or not 0 <= value['received'] <= size
            or type(value.get('slot_touched')) is not bool
            or value.get('state') not in ('accepted', 'verified', 'committing',
                                         'installed', 'boot_confirmed', 'failed', 'aborted')
            or not isinstance(value.get('error'), str)):
        raise RuntimeError(f'upload status does not match selected transaction: {value!r}')
    return value


def reconcile(factory, boot_id, upload_id, digest, size, timeout=5):
    """Read status after commit, never send commit again or reconnect on refusal."""
    deadline = time.monotonic() + timeout
    with factory() as probe:
        if probe.hello['boot_id'] != boot_id:
            raise RuntimeError('boot changed; RAM transaction record is lost; inspect slot, do not replay commit')
        while True:
            result = snapshot(probe.command('flash.status', target_id=1, upload_id=upload_id),
                              upload_id, digest, size)
            if result['state'] in ('boot_confirmed', 'failed', 'aborted'):
                probe.finish()
                return result
            if time.monotonic() >= deadline:
                probe.finish()
                return result
            time.sleep(0.1)


def transfer(factory, capsule, install):
    """Send once. Disconnect before commit discards stage; lost commit reply queries status."""
    digest = hashlib.sha256(capsule).hexdigest()
    if not 256 < len(capsule) <= TARGET['max_size']:
        raise ValueError('capsule is outside the advertised stage limit')
    with factory() as probe:
        boot_id = probe.hello['boot_id']
        begin = probe.command('flash.begin', target_id=1, board_id='nano_rp2040_connect',
                              format=TARGET['format'], size=len(capsule), sha256=digest)
        upload_id = begin.get('upload_id')
        if type(upload_id) is not int or not 1 <= upload_id <= 0xffffffff:
            raise RuntimeError('begin response has an invalid transaction ID')
        snapshot(begin, upload_id, digest, len(capsule))
        if begin['state'] != 'accepted' or begin['received'] != 0 or begin['slot_touched']:
            raise RuntimeError('begin did not acknowledge a new RAM stage')
        chunk_size = min(TARGET['max_chunk'], probe.hello['max_payload'] - 8)
        for offset in range(0, len(capsule), chunk_size):
            data = capsule[offset:offset + chunk_size]
            value = snapshot(probe.upload_chunk(upload_id, offset, data), upload_id, digest, len(capsule))
            if value['state'] != 'accepted' or value['received'] != offset + len(data) or value['slot_touched']:
                raise RuntimeError('chunk acknowledgement does not match the staged offset')
        value = snapshot(probe.command('flash.finish', target_id=1, upload_id=upload_id),
                         upload_id, digest, len(capsule))
        if value['state'] != 'verified' or value['slot_touched']:
            raise RuntimeError('finish did not verify the RAM stage')
        commit_reply_lost = False
        if install:
            try:
                value = snapshot(probe.command('flash.commit', target_id=1, upload_id=upload_id),
                                 upload_id, digest, len(capsule))
                if value['state'] != 'committing':
                    raise RuntimeError('unexpected commit acknowledgement')
            except (OSError, RuntimeError) as exc:
                # A valid rejection also reconciles to verified/failed. This
                # path cannot mistake it for installation or resend commit.
                commit_reply_lost = True
                print(f'Commit result uncertain: {exc}; querying status only.', file=sys.stderr)
                probe.close()
        if not commit_reply_lost:
            probe.finish()
    final = reconcile(factory, boot_id, upload_id, digest, len(capsule))
    if not install and (final['state'] != 'aborted' or final['slot_touched']):
        raise RuntimeError('pre-commit disconnect did not dispose the RAM stage')
    return dict(boot_id=boot_id, transaction=final, commit_reply_lost=commit_reply_lost,
                requested_action='install' if install else 'stage_then_disconnect')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--board', choices=('nano_rp2040_connect',), required=True)
    parser.add_argument('--address', required=True)
    parser.add_argument('--device-id', required=True)
    parser.add_argument('--target-id', type=int, choices=(1,), required=True)
    sub = parser.add_subparsers(dest='action', required=True)
    send = sub.add_parser('transfer', help='validate exact local build and stage/install one capsule')
    send.add_argument('--sketch', choices=('blink_a', 'blink_b'), required=True)
    send.add_argument('--resident-dir', type=Path, required=True)
    send.add_argument('--module-dir', type=Path, required=True)
    send.add_argument('--expected-resident-uf2-sha256', required=True)
    send.add_argument('--expected-capsule-sha256', required=True)
    action = send.add_mutually_exclusive_group(required=True)
    action.add_argument('--install', action='store_true', help='explicitly authorize slot replacement after physically arming D2-to-GND')
    action.add_argument('--stage-only', action='store_true', help='deliberate disconnect before commit; no flash write')
    status = sub.add_parser('status', help='read one retained transaction on a recorded boot; never commit')
    status.add_argument('--upload-id', type=int, required=True)
    status.add_argument('--boot-id', required=True)
    args = parser.parse_args()
    if not re.fullmatch('[0-9a-fA-F]{16}', args.device_id):
        parser.error('--device-id must be 16 hex digits')
    factory = lambda: selected_probe(args.address, args.device_id.lower())
    if args.action == 'transfer':
        for name in ('expected_resident_uf2_sha256', 'expected_capsule_sha256'):
            if not re.fullmatch('[0-9a-fA-F]{64}', getattr(args, name)):
                parser.error(f'--{name.replace("_", "-")} must be 64 hex digits')
        capsule = verified_capsule(args.sketch, args.expected_resident_uf2_sha256.lower(),
                                   args.expected_capsule_sha256.lower(), args.resident_dir, args.module_dir)
        result = transfer(factory, capsule, args.install)
        print(json.dumps(result, indent=2))
        if args.install and result['transaction']['state'] != 'boot_confirmed':
            raise SystemExit('Installation/run is not confirmed; inspect status and preserve ROM recovery. Do not replay commit.')
    else:
        if not 1 <= args.upload_id <= 0xffffffff or not re.fullmatch('[0-9a-f]{16}', args.boot_id):
            parser.error('--upload-id must be a nonzero u32 and --boot-id 16 lowercase hex digits')
        with factory() as probe:
            if probe.hello['boot_id'] != args.boot_id:
                raise RuntimeError('boot changed; retained RAM upload status cannot establish prior install outcome')
            print(json.dumps(probe.command('flash.status', target_id=1, upload_id=args.upload_id), indent=2))
            probe.finish()


if __name__ == '__main__':
    main()
