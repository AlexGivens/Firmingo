"""Fault harness intent/replay checks with a non-executable FMS2 parser fixture."""
import hashlib
from pathlib import Path

import pytest
from experiments.managed_sketch.checkpoint_flash import transfer

CAPSULE = bytes.fromhex((Path(__file__).parents[1] / 'fixtures/managed-stateful-v2/initialized.hex').read_text())


class Probe:
    def __init__(self, *, lost=False, bad_finish=False):
        self.hello = dict(boot_id='aaaaaaaaaaaaaaaa')
        self.operations = []
        self.received = 0
        self.state = 'accepted'
        self.lost, self.bad_finish = lost, bad_finish
        self.closed = False

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.closed = True

    def value(self):
        return dict(upload_id=7, size=len(CAPSULE), sha256=hashlib.sha256(CAPSULE).hexdigest(),
                    received=self.received, state=self.state, slot_touched=False, error='none')

    def command(self, op, **fields):
        assert fields['target_id'] == 2
        self.operations.append(op)
        if op == 'flash.begin':
            assert fields['format'] == 'nano-managed-v2'
        elif op == 'flash.finish':
            self.state = 'accepted' if self.bad_finish else 'verified'
        elif op == 'flash.commit':
            self.state = 'committing'
            if self.lost:
                raise TimeoutError('actual reply unavailable')
        else:
            pytest.fail('harness must never query or retry during the checkpoint')
        return self.value()

    def upload_chunk(self, transaction, offset, part):
        assert transaction == 7 and offset == self.received
        assert part == CAPSULE[offset:offset + len(part)]
        self.received += len(part)
        return self.value()


@pytest.mark.parametrize('lost', [False, True])
def test_one_durable_commit_intent_and_no_status_or_replay(lost):
    import copy
    probe, events = Probe(lost=lost), []
    def save(event):
        events.append(copy.deepcopy(event))
        if event['state'] == 'commitIntent':
            assert 'flash.commit' not in probe.operations
    result = transfer(probe, CAPSULE, save)
    assert [event['state'] for event in events] == ['staging', 'commitIntent', 'outcomeUnknown' if lost else 'committing']
    assert result['commitAttempts'] == 1
    assert probe.operations == ['flash.begin', 'flash.finish', 'flash.commit']
    assert probe.closed and not result['automaticReplay']


def test_failed_commit_intent_persistence_prevents_send():
    probe = Probe()
    def save(event):
        if event['state'] == 'commitIntent':
            raise OSError('durability failure')
    with pytest.raises(OSError, match='durability'):
        transfer(probe, CAPSULE, save)
    assert 'flash.commit' not in probe.operations
    assert probe.closed


def test_unverified_stage_never_commits():
    probe = Probe(bad_finish=True)
    with pytest.raises(RuntimeError, match='RAM verification'):
        transfer(probe, CAPSULE, lambda _: None)
    assert 'flash.commit' not in probe.operations
    assert probe.closed
