"""Read-only macOS USB detach/reattach qualification for one selected Nano."""
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import ipaddress
import json
from pathlib import Path
import platform
import plistlib
import re
import subprocess
import time

from tools.smoke import VerificationError, echo, identity, source, validate_target


def command(argv):
    return subprocess.run(argv, capture_output=True, timeout=5)


def text(result):
    return result.stdout.decode('utf-8', errors='replace')


def successful(argv):
    result = command(argv)
    if result.returncode:
        raise VerificationError(f"host observation failed: {' '.join(argv)}: {result.stderr.decode('utf-8', errors='replace').strip()}")
    return text(result)


def selected_usb(tree, device_id):
    found = []
    def visit(node):
        if isinstance(node, list):
            for child in node:
                visit(child)
        elif isinstance(node, dict):
            if (node.get('IOObjectClass') == 'IOUSBHostDevice'
                    and node.get('idVendor') == 0x2341 and node.get('idProduct') == 0x005e
                    and str(node.get('USB Serial Number', '')).lower() == device_id.lower()):
                found.append({'device_id': node['USB Serial Number'],
                              'registry_id': node['IORegistryEntryID']})
            visit(node.get('IORegistryEntryChildren', []))
    visit(tree)
    if len(found) > 1:
        raise VerificationError('multiple USB devices match the selected Nano identity')
    return found[0] if found else None


def usb(device_id):
    result = command(['ioreg', '-r', '-c', 'IOUSBHostDevice', '-a'])
    if result.returncode:
        raise VerificationError('USB registry observation failed; not interpreted as unplugged')
    return selected_usb(plistlib.loads(result.stdout), device_id)


def route_fields(output):
    return {name: value for name, value in re.findall(r'^\s*(interface|gateway):\s*(\S+)', output, re.M)}


def network_ready(interface_output, packet, address):
    """None means not ready yet. Reject unsafe/wrong DHCP before test traffic."""
    host = re.search(r'\binet (\d+\.\d+\.\d+\.\d+) netmask (0x[0-9a-fA-F]+)', interface_output)
    if not host or 'status: active' not in interface_output or not packet.strip():
        return None
    options = packet.partition('options:')[2]
    if not options:
        raise VerificationError('host DHCP packet lacks decoded options')
    if re.search(r'^\s*(router|domain_name_server|classless_static_route|static_route)\s*\(', options, re.M):
        raise VerificationError('DHCP advertises router, DNS or routes; local-only check failed')
    server = re.search(r'^\s*server_identifier \(ip\):\s*(\S+)', options, re.M)
    if not server or server[1] != address:
        raise VerificationError('DHCP server identifier does not match selected board address')
    if not re.search(r'dhcp_message_type .*: ACK\b', options):
        return None
    if int(host[2], 16) != 0xffffff00 or not re.search(r'subnet_mask \(ip\): 255\.255\.255\.0', options):
        raise VerificationError('qualification requires a /24 DHCP mask')
    if not re.search(r'lease_time \(uint32\):', options):
        raise VerificationError('DHCP lease information missing')
    subnet = ipaddress.IPv4Network(address + '/24', strict=False)
    host_ip = ipaddress.IPv4Address(host[1])
    if host_ip not in subnet or host[1] == address or host_ip in (subnet.network_address, subnet.broadcast_address):
        raise VerificationError('host address does not match the selected local-only subnet')
    xid = re.search(r'^xid = (\S+)', packet, re.M)
    return {'host_address': host[1], 'dhcp_xid': xid[1] if xid else None,
            'interface_output': interface_output, 'dhcp_packet': packet}


def network(interface, address):
    result = command(['ifconfig', interface])
    if result.returncode:
        if 'does not exist' in result.stderr.decode('utf-8', errors='replace'):
            return None
        raise VerificationError('cannot observe network interface')
    packet = command(['ipconfig', 'getpacket', interface])
    # ipconfig emits no packet while DHCP is not ready. The preflight proves
    # access to these commands before monitoring; a timeout is still an error.
    return network_ready(text(result), text(packet), address)


def diagnostics_ready(snapshot):
    if snapshot.get('usb_profile') != 'ncm-only' or snapshot.get('usb_initialized_at_setup') is not False:
        raise VerificationError('unexpected USB profile or controller already initialized before setup')
    names = ['setup_ms', 'dhcp_ready_ms', 'ncm_ready_ms', 'services_ready_ms', 'attach_requested_ms']
    values = [snapshot.get(name) for name in names]
    if any(type(v) is not int or v < 0 for v in values) or values != sorted(values):
        raise VerificationError('invalid service-before-attach startup timestamps')


def wait_for(probe, timeout, clock=time.monotonic, sleep=time.sleep):
    """Bounded observation polling. Exceptions stop the test, never imply absence."""
    start = clock()
    last_negative = None
    while clock() - start < timeout:
        sample_start = clock()
        value = probe()
        sample_end = clock()
        if sample_end - start >= timeout:
            break
        if value:
            return value, {'sample_start': sample_start, 'sample_end': sample_end,
                           'last_negative_sample_start': last_negative}
        last_negative = sample_start
        sleep(min(.2, max(0, timeout - (clock() - start))))
    raise TimeoutError(f'observation deadline expired after {timeout:g} seconds')


def save(path, report):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(json.dumps(report, indent=2) + '\n')
    temporary.replace(path)


def traffic(address):
    def timed(fn, **kwargs):
        begin = time.monotonic()
        result = fn(address, **kwargs)
        return {'begin_monotonic': begin, 'end_monotonic': time.monotonic(), 'result': result}
    with ThreadPoolExecutor(max_workers=2) as pool:
        echoed = pool.submit(timed, echo, byte_count=65536, timeout=15, slow_read=True)
        sourced = pool.submit(timed, source, records=100, timeout=15)
        requests = []
        for family in (['-4'], []):
            begin = time.monotonic()
            result = subprocess.run(['curl', *family, '--fail', '--silent', '--show-error',
                '--max-time', '10', '--output', '/dev/null', '--write-out', '%{http_code} %{remote_ip}',
                f'https://example.com/?firmingo_attach={time.time_ns()}'], capture_output=True, text=True, timeout=12)
            requests.append({'options': family, 'begin_monotonic': begin, 'end_monotonic': time.monotonic(),
                             'exit': result.returncode, 'output': result.stdout, 'error': result.stderr})
        result = {'echo': echoed.result(), 'source': sourced.result(), 'https': requests}
    result['reconnect'] = echo(address, byte_count=4096, timeout=5)
    result['internet_access_pass'] = all(r['exit'] == 0 for r in requests)
    result['internet_overlap_pass'] = all(
        result[k]['begin_monotonic'] <= r['begin_monotonic'] <= r['end_monotonic'] <= result[k]['end_monotonic']
        for r in requests for k in ['echo', 'source'])
    return result


def run(args):
    if platform.system() != 'Darwin':
        raise RuntimeError('attach currently supports macOS only; it cannot qualify iOS')
    address = validate_target(args.address)
    if not re.fullmatch(r'[0-9a-fA-F]{16}', args.device_id):
        raise ValueError('select the expected 16-hex-digit Nano ID')
    if not re.fullmatch(r'[0-9a-fA-F]{64}', args.firmware_sha256):
        raise ValueError('select the SHA-256 of the flashed UF2')
    if args.start_cycle < 1 or not 1 <= args.cycles <= 20:
        raise ValueError('cycle number must be positive; select 1 through 20 cycles')
    if any(not 0 < value <= 600 for value in (args.unplug_timeout, args.plug_timeout, args.ready_timeout)):
        raise ValueError('observation timeouts must be greater than zero and at most 600 seconds')
    subnet = ipaddress.IPv4Network(address + '/24', strict=False)
    if ipaddress.IPv4Address(address) in (subnet.network_address, subnet.broadcast_address):
        raise ValueError('select a unicast board address within its /24')
    if not re.fullmatch(r'en[0-9]+', args.interface):
        raise ValueError('select the actual Nano Ethernet interface, such as en5')
    output = Path(args.output)
    if output.exists():
        raise RuntimeError('output already exists; select a new file to preserve prior evidence')
    report = {'utc': datetime.now(timezone.utc).isoformat(), 'board': args.board,
              'address': address, 'device_id': args.device_id.lower(), 'interface': args.interface,
              'firmware_sha256_user_selected': args.firmware_sha256,
              'connection_details_user_reported': args.connection_details,
              'host': {'model': successful(['sysctl', '-n', 'hw.model']).strip(),
                       'os': successful(['sw_vers']).strip()},
              'timing_basis': 'host-observed USB serial appearance, NOT physical insertion or packet-level lease time',
              'authorization': 'not automatically observed; annotate per cycle after user report',
              'status': 'preflight', 'cycles': []}
    save(output, report)
    try:
        previous = usb(args.device_id)
        if not previous:
            raise VerificationError('selected Nano must be connected before arming this check')
        initial = network(args.interface, address)
        if not initial:
            raise VerificationError('selected interface is not DHCP-ready before arming')
        board_route = successful(['route', '-n', 'get', address])
        default_route = successful(['route', '-n', 'get', 'default'])
        baseline_route = route_fields(default_route)
        if route_fields(board_route).get('interface') != args.interface or baseline_route.get('interface') == args.interface:
            raise VerificationError('preflight routes do not separate the board from Internet access')
        snapshot = identity(address, args.device_id, args.board)
        diagnostics_ready(snapshot)
        report['preflight'] = {'usb': previous, 'network': initial, 'diagnostics': snapshot,
                               'board_route': board_route, 'internet_route': default_route}
        report['status'] = 'armed'
        save(output, report)
        for number in range(args.start_cycle, args.start_cycle + args.cycles):
            cycle = {'cycle': number, 'status': 'waiting_for_unplug', 'usb_detach_reattach_observed': False}
            report['cycles'].append(cycle)
            save(output, report)
            print(f'Cycle {number}: armed. Unplug the Nano USB cable; then reconnect it. Do not press RESET or change IP settings.', flush=True)
            _, detached = wait_for(lambda: not usb(args.device_id), args.unplug_timeout)
            cycle.update(status='waiting_for_plug', detached_observation=detached)
            save(output, report)
            print('Selected Nano disappeared. Waiting for USB appearance.', flush=True)
            present, arrived = wait_for(lambda: usb(args.device_id), args.plug_timeout)
            cycle.update(status='waiting_for_network', usb_detach_reattach_observed=True,
                         usb=present, arrived_observation=arrived)
            save(output, report)
            if present['registry_id'] == previous['registry_id']:
                raise VerificationError('USB instance did not change; no new attachment established')
            readiness_start = arrived['sample_end']
            # Observation polling is limited to USB/address readiness. Identity,
            # byte tests and Internet requests each run once, without retries.
            budget = args.ready_timeout - (time.monotonic() - readiness_start)
            if budget <= 0:
                raise TimeoutError('network readiness deadline expired')
            ready, observed = wait_for(lambda: network(args.interface, address), budget)
            cycle['network'] = ready
            cycle['host_address_observation'] = observed
            cycle['observed_usb_to_address_seconds'] = round(observed['sample_end'] - arrived['sample_end'], 6)
            board_route = successful(['route', '-n', 'get', address])
            default_route = successful(['route', '-n', 'get', 'default'])
            cycle.update(board_route=board_route, internet_route=default_route)
            if route_fields(board_route).get('interface') != args.interface or route_fields(default_route) != baseline_route:
                raise VerificationError('board or Internet route changed from expected preflight state')
            budget = args.ready_timeout - (time.monotonic() - readiness_start)
            if budget <= 0:
                raise TimeoutError('identity readiness deadline expired')
            snapshot = identity(address, args.device_id, args.board, timeout=budget)
            diagnostics_ready(snapshot)
            if time.monotonic() - readiness_start >= args.ready_timeout:
                raise TimeoutError('network/identity readiness deadline expired')
            cycle['diagnostics'] = snapshot
            cycle['observed_usb_to_identity_seconds'] = round(time.monotonic() - arrived['sample_end'], 6)
            cycle['dhcp_xid_changed'] = ready['dhcp_xid'] != initial['dhcp_xid']
            cycle['status'] = 'checking_bytes_and_internet'
            save(output, report)
            cycle['traffic'] = traffic(address)
            if not cycle['traffic']['internet_access_pass'] or not cycle['traffic']['internet_overlap_pass']:
                raise VerificationError('Internet request failed or was not concurrent with both streams')
            cycle['status'] = 'functional_pass'
            previous, initial = present, ready
            save(output, report)
            print(f"Cycle {number}: functional PASS. Observed USB-to-address {cycle['observed_usb_to_address_seconds']:.3f}s; USB-to-identity {cycle['observed_usb_to_identity_seconds']:.3f}s. Approval prompt remains a manual observation.", flush=True)
        report['status'] = 'complete'
        save(output, report)
        return 0
    except (Exception, KeyboardInterrupt) as exc:
        report['status'] = 'incomplete'
        report['error'] = f'{type(exc).__name__}: {exc}'
        if report['cycles']:
            cycle = report['cycles'][-1]
            if cycle['status'] != 'functional_pass':
                cycle['failed_stage'] = cycle['status']
                cycle['status'] = 'failed' if cycle['usb_detach_reattach_observed'] else 'not_started'
                cycle['error'] = report['error']
        save(output, report)
        raise
