"""Fake I/O verifies soak orchestration/error reports, not hardware integrity."""
import argparse
import json
import pytest
from tools import soak
from tools.version import current_version


@pytest.fixture
def selected(tmp_path, monkeypatch):
    args = argparse.Namespace(board='nano_rp2040_connect', address='127.0.0.1',
        device_id='a1b2c3d4e5f60718', firmware_sha256='a'*64, output=str(tmp_path/'result.json'),
        firmware='application', duration=2, byte_count=1024, batch_timeout=5,
        reconnect_every=2, seed=0x12345678)
    class Clock:
        tick = 0
        def monotonic(self):
            return self.tick
    clock = Clock()
    monkeypatch.setattr(soak.time, 'monotonic', clock.monotonic)
    instances, seeds = [], []
    class Probe:
        fault = None
        def __init__(self, address, device_id, board, timeout, firmware_version, backend):
            assert (address,device_id,board,timeout)==(args.address,args.device_id,args.board,5)
            expected = (current_version(),'uart') if args.firmware == 'uart' else (current_version(),'application')
            assert (firmware_version,backend) == expected
            self.hello = {'boot_id': '0123456789abcdef'}
            if args.firmware == 'uart':
                self.hello['channels'] = [dict(id=1,backend='uart',controls=[],rx_queue=256,tx_queue=256,
                    configurable=['baud','data_bits','parity','stop_bits'],baud_min=300,baud_max=2000000,
                    config=dict(baud=57600,actual_baud=57597,data_bits=8,parity='none',stop_bits=1,
                                flow_control='none'))]
            if self.fault=='boot' and instances:self.hello['boot_id']='0000000000000000'
            self.bytes, self.closed, self.finished, self.open_config = 0, False, False, None
            instances.append(self)
        def command(self, op, **_fields):
            if op=='device.diagnostics':
                if self.fault=='diagnostics':raise RuntimeError('unsupported')
                value = dict(samples=1+len(seeds),heap_free=10000,heap_min=9000,
                            stack_free=3000,stack_min=2500,lwip_free=None,
                            peak_to_backend=256,peak_to_peer=256)
                if args.firmware == 'uart':
                    value.update(uart_rx_pending=0,uart_rx_discarded=0,
                                 uart_rx_overrun_events=0,uart_rx_lost_bytes_minimum=0,
                                 uart_tx_throttles=0)
                else:
                    value.update(application_rx_pending=0,application_tx_pending=0,
                                 application_rx_peak=256,application_tx_peak=256,
                                 application_rx_discarded=0,application_tx_discarded=0)
                return value
            if op=='serial.status':
                return dict(backend_rx_bytes=self.bytes,backend_tx_bytes=self.bytes+(self.fault=='counters'),
                            pending_to_backend=0,pending_to_peer=0)
            assert op in ('serial.open','serial.close')
            if op == 'serial.open': self.open_config = _fields.get('config')
        def echo(self, byte_count, slow_read, seed):
            seeds.append(seed);clock.tick+=0.6
            if self.fault=='transfer':raise RuntimeError('first mismatch at byte 3')
            if self.fault=='interrupt':raise KeyboardInterrupt()
            self.bytes += byte_count
            return dict(bytes=byte_count, seconds=0.6, bytes_per_second=byte_count/0.6,
                        fragmented_sends=10, slow_read=slow_read, seed=seed)
        def finish(self):
            if self.fault=='close':raise RuntimeError('closure was not observed')
            self.finished=True;self.closed=True
        def close(self):
            self.closed=True
    monkeypatch.setattr(soak, 'Probe', Probe)
    return args, Probe, instances, seeds, clock


def result(args):
    return json.loads(open(args.output).read())


def test_exact_aggregate_seed_variation_reconnects_and_memory_limit_are_reported(selected):
    args, _probe, peers, seeds, _clock = selected
    assert soak.run(args)==0
    report=result(args)
    assert report['exact_bytes']==4096 and report['completed_batches']==4
    assert report['completed_sessions']==2 and all(p.finished for p in peers)
    assert len(set(seeds))==4 and seeds[0]==args.seed
    assert report['status']=='passed' and report['firmware_hash_verified_on_device'] is False
    assert report['runtime_memory_measurement'].startswith('unavailable')


@pytest.mark.parametrize('fault,status', [('transfer','failed'),('counters','failed'),
                                         ('boot','failed'),('close','failed'),('interrupt','interrupted')])
def test_failure_and_interruption_never_retry_or_pass(selected,fault,status):
    args, probe, peers, seeds, _clock=selected;probe.fault=fault
    assert soak.run(args)==1
    report=result(args)
    assert report['status']==status and 'error' in report
    assert all(p.closed for p in peers)
    assert len(peers)<=2
    if fault=='transfer':
        assert len(seeds)==1 and report['exact_bytes']==0
        assert report['in_progress_batch']['seed']==args.seed
    if fault=='boot':assert len(seeds)==2


def test_existing_evidence_cannot_be_overwritten_or_contact_board(selected):
    args, _probe, peers, _seeds, _clock=selected
    open(args.output,'w').write('preserve')
    with pytest.raises(FileExistsError):soak.run(args)
    assert not peers and open(args.output).read()=='preserve'


@pytest.mark.parametrize('name,value', [('duration',0),('duration',3601),('byte_count',262145),
                                     ('batch_timeout',0),('reconnect_every',0),('seed',-1),
                                     ('address','example.com'),('device_id','wrong'),('firmware_sha256','wrong')])
def test_bad_selection_fails_before_evidence_or_socket(selected,name,value):
    args,_probe,peers,_seeds,_clock=selected;setattr(args,name,value)
    with pytest.raises(ValueError):soak.run(args)
    assert not peers
    from pathlib import Path
    assert not Path(args.output).exists()


def test_uppercase_device_id_is_validated_and_canonicalized(selected):
    args,_probe,peers,_seeds,_clock=selected
    args.device_id='B2C3D4E5F6071829'
    soak.validate(args)
    assert args.device_id=='b2c3d4e5f6071829'
    assert not peers


def test_detail_storage_remains_bounded_during_many_batches(selected,monkeypatch):
    args,_probe,_peers,_seeds,_clock=selected
    monkeypatch.setattr(soak,'MAX_SAMPLES',2)
    assert soak.run(args)==0
    report=result(args)
    assert len(report['samples'])==2 and report['omitted_samples']==2
    assert report['last_sample']['batch']==4


def test_required_diagnostics_record_initial_and_per_batch_samples(selected):
    args,_probe,peers,_seeds,_clock=selected;args.diagnostics=True
    assert soak.run(args)==0
    report=result(args)
    assert report['initial_diagnostics']['samples']==1
    assert report['last_diagnostics']['samples']==5
    assert all(s['diagnostics']['heap_min']==9000 for s in report['samples'])
    assert report['last_diagnostics']['application_rx_pending']==0
    assert report['runtime_memory_measurement'].startswith('sampled')


def test_uart_profile_reconfigures_each_session_and_records_uart_diagnostics(selected):
    args,_probe,peers,_seeds,_clock=selected
    args.firmware='uart';args.diagnostics=True
    assert soak.run(args)==0
    report=result(args)
    assert report['firmware']=='uart'
    assert all(peer.open_config==soak.TEST_CONFIGURATION for peer in peers)
    assert report['initial_diagnostics']['uart_rx_overrun_events']==0
    assert report['last_diagnostics']['uart_rx_lost_bytes_minimum']==0


def test_unsupported_diagnostics_fail_without_fallback_or_echo(selected):
    args,probe,peers,seeds,_clock=selected;args.diagnostics=True;probe.fault='diagnostics'
    assert soak.run(args)==1
    assert not seeds and len(peers)==1 and peers[0].closed
    assert result(args)['status']=='failed'


@pytest.mark.parametrize('field,value', [('heap_free',True),('samples',0),('heap_min',10001),
    ('stack_min',3001),('peak_to_backend',257),('lwip_free',0)])
def test_invalid_diagnostics_never_become_measurement(field,value):
    class Peer:
        def command(self,op):
            result=dict(samples=1,heap_free=10000,heap_min=9000,stack_free=3000,
                        stack_min=2500,lwip_free=None,peak_to_backend=256,peak_to_peer=256)
            result[field]=value;return result
    with pytest.raises(RuntimeError):soak.diagnostics(Peer())


def test_diagnostics_minima_and_queue_peaks_cannot_reset_after_reconnect():
    class Peer:
        def command(self,op):
            return dict(samples=20,heap_free=10000,heap_min=9000,stack_free=3000,
                        stack_min=2500,lwip_free=None,peak_to_backend=256,peak_to_peer=256)
    previous=soak.diagnostics(Peer());previous['heap_min']=8999
    with pytest.raises(RuntimeError,match='lifetime'):soak.diagnostics(Peer(),previous)


def test_application_diagnostics_are_all_or_none_and_bounded():
    class Peer:
        def __init__(self,value):self.value=value
        def command(self,op):return self.value
    base=dict(samples=1,heap_free=10000,heap_min=9000,stack_free=3000,
              stack_min=2500,lwip_free=None,peak_to_backend=256,peak_to_peer=256)
    partial=dict(base,application_rx_pending=0)
    with pytest.raises(RuntimeError,match='incomplete'):soak.diagnostics(Peer(partial))
    complete=dict(base,application_rx_pending=0,application_tx_pending=0,
                  application_rx_peak=257,application_tx_peak=256,
                  application_rx_discarded=0,application_tx_discarded=0)
    with pytest.raises(RuntimeError,match='inconsistent'):soak.diagnostics(Peer(complete))
    valid=dict(complete,application_rx_peak=256)
    previous=soak.diagnostics(Peer(valid))
    with pytest.raises(RuntimeError,match='availability changed'):
        soak.diagnostics(Peer(base),previous)


def test_uart_diagnostics_are_all_or_none_bounded_and_monotonic():
    class Peer:
        def __init__(self,value):self.value=value
        def command(self,op):return self.value
    base=dict(samples=1,heap_free=10000,heap_min=9000,stack_free=3000,
              stack_min=2500,lwip_free=None,peak_to_backend=0,peak_to_peer=0)
    with pytest.raises(RuntimeError,match='incomplete UART'):
        soak.diagnostics(Peer(dict(base,uart_rx_pending=0)))
    valid=dict(base,uart_rx_pending=0,uart_rx_discarded=0,
               uart_rx_overrun_events=0,uart_rx_lost_bytes_minimum=0,
               uart_tx_throttles=0)
    previous=soak.diagnostics(Peer(valid))
    with pytest.raises(RuntimeError,match='inconsistent'):
        soak.diagnostics(Peer(dict(valid,uart_rx_pending=257)))
    with pytest.raises(RuntimeError,match='lifetime'):
        soak.diagnostics(Peer(dict(valid,uart_rx_discarded=0)),dict(previous,uart_rx_discarded=1))
    with pytest.raises(RuntimeError,match='UART diagnostics availability changed'):
        soak.diagnostics(Peer(base),previous)


def test_transport_diagnostics_are_all_or_none_bounded_and_monotonic():
    class Peer:
        def __init__(self,value):self.value=value
        def command(self,op):return self.value
    base=dict(samples=1,heap_free=10000,heap_min=9000,stack_free=3000,
              stack_min=2500,lwip_free=None,peak_to_backend=0,peak_to_peer=0)
    fields=('ncm_worker_runs','ncm_rx_frames','ncm_rx_deferred','ncm_rx_batch_peak','ncm_mutex_contentions',
            'ncm_budget_exhaustions','ncm_wake_requests','tcp_accepts',
            'tcp_rx_callbacks','tcp_sent_callbacks','tcp_errors',
            'tcp_rx_bytes','tcp_sent_bytes')
    with pytest.raises(RuntimeError,match='incomplete transport'):
        soak.diagnostics(Peer(dict(base,ncm_worker_runs=1)))
    valid=dict(base,**{name:1 for name in fields})
    previous=soak.diagnostics(Peer(valid))
    with pytest.raises(RuntimeError,match='inconsistent'):
        soak.diagnostics(Peer(dict(valid,ncm_rx_batch_peak=11)))
    changed=dict(valid,ncm_worker_runs=0)
    with pytest.raises(RuntimeError,match='lifetime'):
        soak.diagnostics(Peer(changed),previous)
    missing=dict(base)
    with pytest.raises(RuntimeError,match='transport diagnostics availability changed'):
        soak.diagnostics(Peer(missing),previous)
