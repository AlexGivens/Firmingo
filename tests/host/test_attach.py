"""Fake host observations exercise qualification orchestration, not USB hardware."""
import argparse
import json
import pytest
from tools import attach
from tools.smoke import VerificationError

ID='a1b2c3d4e5f60718'
USB={'IOObjectClass':'IOUSBHostDevice','idVendor':0x2341,'idProduct':0x005e,
     'USB Serial Number':ID,'IORegistryEntryID':123}
IFACE='inet 192.168.77.16 netmask 0xffffff00\nstatus: active\n'
PACKET='xid = 0x123\noptions:\ndhcp_message_type (uint8): ACK 0x5\nserver_identifier (ip): 192.168.77.1\nsubnet_mask (ip): 255.255.255.0\nlease_time (uint32): 0x258\nend (none):\n'
DIAG={'usb_profile':'ncm-only','usb_initialized_at_setup':False,'setup_ms':1,
      'dhcp_ready_ms':1,'ncm_ready_ms':2,'services_ready_ms':2,'attach_requested_ms':2}


def test_usb_observer_filters_other_devices_and_nested_interfaces():
    child=dict(USB,IOObjectClass='IOUSBHostInterface')
    node=dict(USB,IORegistryEntryChildren=[child])
    other=dict(USB,**{'USB Serial Number':'other'})
    assert attach.selected_usb([other,{'IORegistryEntryChildren':[node]}],ID)=={'device_id':ID,'registry_id':123}
    assert attach.selected_usb([other],ID) is None
    with pytest.raises(VerificationError,match='multiple'):
        attach.selected_usb([node,USB],ID)


def test_valid_local_only_packet_and_not_ready_observations():
    ready=attach.network_ready(IFACE,PACKET,'192.168.77.1')
    assert ready['host_address']=='192.168.77.16'
    assert ready['dhcp_xid']=='0x123'
    assert attach.network_ready('',PACKET,'192.168.77.1') is None
    assert attach.network_ready(IFACE,'','192.168.77.1') is None


@pytest.mark.parametrize('fault',['router','dns','route','server','mask','lease','host','broadcast'])
def test_wrong_or_unsafe_network_is_rejected_before_test_bytes(fault):
    packet=PACKET; interface=IFACE
    if fault=='router': packet+='router (ip_mult): {192.168.77.1}\n'
    elif fault=='dns': packet+='domain_name_server (ip_mult): {192.168.77.1}\n'
    elif fault=='route': packet+='classless_static_route (uint8_mult): {}\n'
    elif fault=='server': packet=packet.replace('server_identifier (ip): 192.168.77.1','server_identifier (ip): 192.168.4.1')
    elif fault=='mask': packet=packet.replace('255.255.255.0','255.255.0.0')
    elif fault=='lease': packet=packet.replace('lease_time (uint32): 0x258\n','')
    elif fault=='host': interface=interface.replace('192.168.77.16','192.168.4.16')
    else: interface=interface.replace('192.168.77.16','192.168.77.255')
    with pytest.raises(VerificationError):
        attach.network_ready(interface,packet,'192.168.77.1')


class Clock:
    value=0
    def now(self): return self.value
    def sleep(self,seconds): self.value+=seconds


def test_observation_wait_has_one_deadline_and_reports_sample_interval():
    clock=Clock(); samples=iter([False,False,{'ready':True}])
    value,timing=attach.wait_for(lambda: next(samples),1,clock.now,clock.sleep)
    assert value=={'ready':True}
    assert timing=={'sample_start':.4,'sample_end':.4,'last_negative_sample_start':.2}
    with pytest.raises(TimeoutError):
        attach.wait_for(lambda: False,.5,clock.now,clock.sleep)


def test_probe_error_is_never_treated_as_absence_and_late_success_is_timeout():
    clock=Clock()
    def fail(): raise VerificationError('observation denied')
    with pytest.raises(VerificationError): attach.wait_for(fail,1,clock.now,clock.sleep)
    def late(): clock.sleep(2); return True
    with pytest.raises(TimeoutError): attach.wait_for(late,1,clock.now,clock.sleep)


@pytest.mark.parametrize('change',[{'usb_initialized_at_setup':True},{'usb_profile':'composite'},
    {'attach_requested_ms':1},{'setup_ms':True},{'ncm_ready_ms':None}])
def test_diagnostics_reject_unready_or_inconsistent_startup(change):
    with pytest.raises(VerificationError): attach.diagnostics_ready(dict(DIAG,**change))


@pytest.fixture
def monitored(tmp_path,monkeypatch):
    monkeypatch.setattr(attach.platform,'system',lambda:'Darwin')
    def successful(argv):
        if argv[0]=='route':return 'interface: '+('en5' if argv[-1]=='192.168.77.1' else 'en0')+'\ngateway: 192.168.4.1\n'
        return 'fake host metadata'
    monkeypatch.setattr(attach,'successful',successful)
    snapshots=iter([{'device_id':ID,'registry_id':1},None,{'device_id':ID,'registry_id':2}])
    monkeypatch.setattr(attach,'usb',lambda _:next(snapshots))
    monkeypatch.setattr(attach,'network',lambda *_: {'host_address':'192.168.77.16','dhcp_xid':'0x123'})
    monkeypatch.setattr(attach,'identity',lambda *_,**kw: dict(DIAG))
    monkeypatch.setattr(attach,'traffic',lambda _: {'internet_access_pass':True,'internet_overlap_pass':True})
    args=argparse.Namespace(board='nano_rp2040_connect',address='192.168.77.1',device_id=ID,
        interface='en5',output=str(tmp_path/'result.json'),firmware_sha256='a'*64,
        connection_details='fake hardware',cycles=1,start_cycle=2,unplug_timeout=1,plug_timeout=1,ready_timeout=1)
    return args


def test_completed_observation_saves_all_stages_and_preserves_old_output(monitored):
    assert attach.run(monitored)==0
    report=json.load(open(monitored.output))
    assert report['status']=='complete'
    assert report['cycles'][0]['cycle']==2
    assert report['cycles'][0]['status']=='functional_pass'
    with pytest.raises(RuntimeError,match='already exists'):attach.run(monitored)


def test_missing_user_unplug_is_not_a_physical_attempt(monitored,monkeypatch):
    monkeypatch.setattr(attach,'usb',lambda _: {'device_id':ID,'registry_id':1})
    monitored.unplug_timeout=.01
    with pytest.raises(TimeoutError):attach.run(monitored)
    cycle=json.load(open(monitored.output))['cycles'][0]
    assert cycle['status']=='not_started'
    assert cycle['usb_detach_reattach_observed'] is False


def test_byte_failure_is_saved_without_retry_or_pass(monitored,monkeypatch):
    calls=[]
    def fail(_): calls.append(1); raise VerificationError('echo mismatch')
    monkeypatch.setattr(attach,'traffic',fail)
    with pytest.raises(VerificationError,match='echo mismatch'):attach.run(monitored)
    cycle=json.load(open(monitored.output))['cycles'][0]
    assert cycle['status']=='failed'
    assert cycle['failed_stage']=='checking_bytes_and_internet'
    assert calls==[1]


def test_route_mismatch_stops_before_identity_or_bytes(monitored,monkeypatch):
    routes=iter(['interface: en5\n','interface: en0\ngateway: 192.168.4.1\n',
                 'interface: en0\n','interface: en0\ngateway: 192.168.4.1\n'])
    monkeypatch.setattr(attach,'successful',lambda argv: next(routes) if argv[0]=='route' else 'fake host')
    calls=[]
    monkeypatch.setattr(attach,'identity',lambda *_,**kw: calls.append('identity') or dict(DIAG))
    monkeypatch.setattr(attach,'traffic',lambda _:calls.append('bytes'))
    with pytest.raises(VerificationError,match='route changed'):attach.run(monitored)
    assert calls==['identity'] # preflight only
