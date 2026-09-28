"""M9 host harness unit checks; fake peers are not Nano install evidence."""
import hashlib
import json
from pathlib import Path
import struct

import pytest
from experiments.managed_sketch import fmgo_upload as upload
from experiments.managed_sketch.upload import verified_capsule
from tools.session_smoke import Probe, HEADER, frame

FIXTURES = Path(__file__).parents[1] / 'fixtures/protocol-v1/managed-upload'


def fixture(name):
    return bytes.fromhex((FIXTURES / (name + '.hex')).read_text())


def capsule():
    return fixture('chunk')[HEADER.size + 8:]


class Device:
    def __init__(self, lost_commit=False, reboot=False, foreign_digest=False):
        self.events = []
        self.state = None
        self.received = 0
        self.lost_commit = lost_commit
        self.reboot = reboot
        self.foreign_digest = foreign_digest
        self.connections = 0
        self.digest = hashlib.sha256(capsule()).hexdigest()

    def factory(self):
        self.connections += 1
        return FakeProbe(self)


class FakeProbe:
    def __init__(self, device):
        self.device = device
        self.hello = dict(boot_id=('bbbbbbbbbbbbbbbb' if device.reboot and device.connections > 1
                                  else 'aaaaaaaaaaaaaaaa'), max_payload=4096)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def status(self):
        return dict(upload_id=3, state=self.device.state, size=len(capsule()),
                    received=self.device.received, sha256=('0' * 64 if self.device.foreign_digest
                                                         else self.device.digest),
                    slot_touched=self.device.state in ('committing', 'boot_confirmed'), error='none')

    def command(self, op, **fields):
        self.device.events.append(op)
        assert fields['target_id'] == 1
        if op == 'flash.begin':
            assert fields['format'] == 'nano-managed-v1'
            assert fields['sha256'] == self.device.digest
            self.device.state = 'accepted'
        elif op == 'flash.finish':
            assert self.device.received == len(capsule())
            self.device.state = 'verified'
        elif op == 'flash.commit':
            assert self.device.state == 'verified'
            self.device.state = 'committing'
            if self.device.lost_commit:
                raise OSError('lost response after commit acceptance')
        elif op == 'flash.status' and self.device.state == 'committing':
            self.device.state = 'boot_confirmed'
        return self.status()

    def upload_chunk(self, upload_id, offset, data):
        assert upload_id == 3 and offset == self.device.received
        assert data == capsule()[offset:offset + len(data)]
        self.device.events.append('chunk')
        self.device.received += len(data)
        return self.status()

    def close(self):
        if self.device.state in ('accepted', 'verified'):
            self.device.state = 'aborted'

    def finish(self):
        self.close()


@pytest.mark.parametrize('lost_commit', [False, True])
def test_install_sends_one_commit_then_reconciles_read_only(lost_commit):
    device = Device(lost_commit=lost_commit)
    result = upload.transfer(device.factory, capsule(), True)
    assert result['transaction']['state'] == 'boot_confirmed'
    assert result['commit_reply_lost'] == lost_commit
    assert device.events.count('flash.commit') == 1
    assert device.events[-1] == 'flash.status'
    assert device.connections == 2


def test_precommit_disconnect_is_reported_aborted_without_commit():
    device = Device()
    result = upload.transfer(device.factory, capsule(), False)
    assert result['transaction']['state'] == 'aborted'
    assert not result['transaction']['slot_touched']
    assert 'flash.commit' not in device.events


def test_reboot_after_commit_is_uncertain_and_never_replays():
    device = Device(reboot=True)
    with pytest.raises(RuntimeError, match='boot changed'):
        upload.transfer(device.factory, capsule(), True)
    assert device.events.count('flash.commit') == 1


def test_foreign_status_hash_is_rejected_before_chunks_or_commit():
    device = Device(foreign_digest=True)
    with pytest.raises(RuntimeError, match='does not match'):
        upload.transfer(device.factory, capsule(), True)
    assert 'chunk' not in device.events and 'flash.commit' not in device.events


def test_wire_vectors_match_host_json_and_binary_encoding():
    payload = fixture('begin-request')[HEADER.size:]
    assert frame(1, 9, payload) == fixture('begin-request')
    value = json.loads(payload)
    assert value['sha256'] == hashlib.sha256(capsule()).hexdigest()
    assert frame(4, 9, struct.pack('!II', 1, 0) + capsule()) == fixture('chunk')
    for op in ('finish', 'status', 'commit', 'abort'):
        body = json.dumps(dict(op='flash.' + op, target_id=1, upload_id=1), separators=(',', ':')).encode()
        assert frame(1, 9, body) == fixture(op + '-request')


def test_upload_chunk_uses_correlated_binary_request_and_preserves_bytes():
    class Socket:
        response = fixture('verified-response')
        written = None

        def settimeout(self, _):
            pass

        def sendall(self, data):
            self.written = data

        def recv(self, count):
            data, self.response = self.response[:count], self.response[count:]
            return data
    probe = object.__new__(Probe)
    probe.sock = Socket()
    probe.timeout, probe.next_request = 5, 9
    probe.upload_chunk(1, 0, capsule())
    assert probe.sock.written == fixture('chunk')


def test_exact_artifact_validation_supports_isolated_build_dirs(tmp_path):
    resident = tmp_path / 'resident'
    artifacts = resident / 'artifacts'
    artifacts.mkdir(parents=True)
    modules = tmp_path / 'modules'
    sketch = modules / 'blink_b'
    sketch.mkdir(parents=True)
    uf2, elf = b'resident-test-artifact', b'elf-test-artifact'
    digest = lambda value: hashlib.sha256(value).hexdigest()
    (artifacts / 'firmingo_managed_resident.ino.uf2').write_bytes(uf2)
    (artifacts / 'firmingo_managed_resident.ino.elf').write_bytes(elf)
    (resident / 'report.json').write_text(json.dumps(dict(board='nano_rp2040_connect',
        status='compile-tested experimental resident; not flashed', uf2_sha256=digest(uf2), elf_sha256=digest(elf))))
    (sketch / 'blink_b.fms').write_bytes(capsule())
    (sketch / 'report.json').write_text(json.dumps(dict(board='nano_rp2040_connect', sketch='blink_b',
        status='compile-tested module only; no resident or hardware proof',
        resident_elf_sha256=digest(elf), capsule_sha256=digest(capsule()))))
    assert verified_capsule('blink_b', digest(uf2), digest(capsule()), resident, modules) == capsule()
    with pytest.raises(ValueError, match='selected exact hash'):
        verified_capsule('blink_b', '0' * 64, digest(capsule()), resident, modules)


def test_snapshot_rejects_boolean_transaction_id():
    value = dict(upload_id=True, state='verified', size=692, received=692,
                 sha256='0' * 64, slot_touched=False, error='none')
    with pytest.raises(RuntimeError, match='does not match'):
        upload.snapshot(value, 1, '0' * 64, 692)
