"""Opt-in bounded stream traffic validation; never flash/reset/retry."""
import json
import re
import time
import shlex
import sys
from pathlib import Path
from tools.session_smoke import Probe
from tools.smoke import validate_target
from tools.uart_smoke import TEST_CONFIGURATION, validate_channel

MAX_SAMPLES = 64


def validate(args):
    validate_target(args.address)
    if not re.fullmatch('[0-9a-fA-F]{16}', args.device_id):
        raise ValueError('device ID must be exactly 16 hexadecimal digits')
    args.device_id = args.device_id.lower()
    if not re.fullmatch('[0-9a-f]{64}', args.firmware_sha256):
        raise ValueError('declared firmware SHA-256 must be 64 lowercase hex digits')
    if getattr(args, 'firmware', 'application') not in ('application', 'uart'):
        raise ValueError('firmware must be application or uart')
    for name, low, high in [('duration', 1, 3600), ('byte_count', 1, 262144),
                            ('batch_timeout', 1, 60), ('reconnect_every', 1, 1000),
                            ('seed', 0, 0xffffffff)]:
        value = getattr(args, name)
        if type(value) is not int or not low <= value <= high:
            raise ValueError(f'{name} must be an integer {low}..{high}')


def diagnostics(peer, previous=None):
    value = peer.command('device.diagnostics')
    base_fields = ('samples','heap_free','heap_min','stack_free','stack_min',
                   'peak_to_backend','peak_to_peer')
    application_fields = ('application_rx_pending','application_tx_pending',
                          'application_rx_peak','application_tx_peak',
                          'application_rx_discarded','application_tx_discarded')
    uart_fields = ('uart_rx_pending','uart_rx_discarded',
                   'uart_rx_overrun_events','uart_rx_lost_bytes_minimum',
                   'uart_tx_throttles')
    transport_narrow = ('ncm_worker_runs','ncm_rx_frames','ncm_rx_deferred',
                        'ncm_rx_batch_peak','ncm_mutex_contentions','ncm_budget_exhaustions',
                        'ncm_wake_requests','tcp_accepts',
                        'tcp_rx_callbacks','tcp_sent_callbacks','tcp_errors')
    transport_wide = ('tcp_rx_bytes','tcp_sent_bytes')
    transport_fields = transport_narrow + transport_wide
    present = tuple(k for k in application_fields if k in value)
    uart_present = tuple(k for k in uart_fields if k in value)
    transport_present = tuple(k for k in transport_fields if k in value)
    if present and present != application_fields:
        raise RuntimeError(f'incomplete application diagnostics: {value!r}')
    if previous and bool(present) != all(k in previous for k in application_fields):
        raise RuntimeError(f'application diagnostics availability changed: {value!r}')
    if uart_present and uart_present != uart_fields:
        raise RuntimeError(f'incomplete UART diagnostics: {value!r}')
    if present and uart_present:
        raise RuntimeError(f'multiple backend diagnostics: {value!r}')
    if previous and bool(uart_present) != all(k in previous for k in uart_fields):
        raise RuntimeError(f'UART diagnostics availability changed: {value!r}')
    if transport_present and transport_present != transport_fields:
        raise RuntimeError(f'incomplete transport diagnostics: {value!r}')
    if previous and bool(transport_present) != all(k in previous for k in transport_fields):
        raise RuntimeError(f'transport diagnostics availability changed: {value!r}')
    fields = base_fields + present + uart_present + transport_present
    narrow = (base_fields + present[:4] + uart_present[:1] + uart_present[2:3]
              + uart_present[4:]
              + transport_present[:len(transport_narrow)])
    wide = (present[4:] + uart_present[1:2] + uart_present[3:]
            + transport_present[len(transport_narrow):])
    if (not isinstance(value, dict) or any(type(value.get(k)) is not int or
            not 0 <= value[k] <= 0xffffffff for k in narrow) or
            any(type(value.get(k)) is not int or not 0 <= value[k] <= 0xffffffffffffffff for k in wide) or
            value.get('samples',0) == 0 or 'lwip_free' not in value or value['lwip_free'] is not None):
        raise RuntimeError(f'invalid firmware diagnostics: {value!r}')
    if (value['heap_min'] > value['heap_free'] or value['stack_min'] > value['stack_free'] or
            value['peak_to_backend'] > 256 or value['peak_to_peer'] > 256 or
            any(value[k] > 256 for k in present[:4]) or
            any(value[k] > 256 for k in uart_present[:1]) or
            ('ncm_rx_batch_peak' in value and value['ncm_rx_batch_peak'] > 10) or
            any(value[k] > 0xffffffffffffffff for k in present[4:])):
        raise RuntimeError(f'inconsistent firmware diagnostics: {value!r}')
    if previous and (value['samples'] < previous['samples'] or
            any(value[k] > previous[k] for k in ('heap_min','stack_min')) or
            any(value[k] < previous[k] for k in ('peak_to_backend','peak_to_peer')) or
            any(value[k] < previous[k] for k in present[2:] if k in previous) or
            any(value[k] < previous[k] for k in uart_present[1:] if k in previous) or
            any(value[k] < previous[k] for k in transport_present if k in previous)):
        raise RuntimeError(f'firmware diagnostics lifetime counters changed: {value!r}')
    return {k:value[k] for k in (*fields,'lwip_free')}


def run(args):
    measured = getattr(args, "diagnostics", False)
    firmware = getattr(args, 'firmware', 'application')
    validate(args) # Reject invalid selections before creating a file/socket.
    report = dict(status='running', board=args.board, address=args.address,
                  firmware=firmware,
                  device_id=args.device_id, declared_firmware_sha256=args.firmware_sha256,
                  firmware_hash_verified_on_device=False,
                  requested_duration_seconds=args.duration, batch_bytes=args.byte_count,
                  batch_timeout_seconds=args.batch_timeout, reconnect_every=args.reconnect_every,
                  seed=args.seed, completed_batches=0, completed_sessions=0,
                  exact_bytes=0, samples=[], omitted_samples=0,
                  runtime_memory_measurement='unavailable; traffic/counters are not heap measurements',
                  started_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()))
    # Reserve exclusively; an existing evidence file is never overwritten.
    with Path(args.output).open('x') as output:
        started = time.monotonic()
        deadline = started + args.duration
        peer = None
        boot = None
        session_bytes = 0
        last_diagnostics = None
        def save():
            report['elapsed_seconds'] = time.monotonic() - started
            output.seek(0)
            json.dump(report, output, indent=2)
            output.write('\n'); output.truncate(); output.flush()
        save()
        try:
            while time.monotonic() < deadline:
                if peer is None:
                    from tools.version import current_version
                    expected_version = current_version()
                    peer = Probe(args.address, args.device_id, args.board, timeout=args.batch_timeout,
                                 firmware_version=expected_version, backend=firmware)
                    observed = peer.hello['boot_id']
                    if boot is None:
                        boot = observed; report['hello'] = peer.hello
                    if observed != boot:
                        raise RuntimeError(f'boot changed: expected {boot}, observed {observed}')
                    if measured:
                        last_diagnostics = diagnostics(peer,last_diagnostics)
                        report.setdefault('initial_diagnostics',last_diagnostics)
                        report['runtime_memory_measurement'] = 'sampled C heap and approximate core-0 stack; lwIP pool unavailable'
                        if firmware == 'uart' and (last_diagnostics['uart_rx_overrun_events']
                                or last_diagnostics['uart_rx_lost_bytes_minimum']):
                            raise RuntimeError(f'UART overrun/loss already reported: {last_diagnostics!r}')
                    if firmware == 'uart':
                        validate_channel(peer.hello['channels'][0])
                        peer.command('serial.open', channel_id=1, config=TEST_CONFIGURATION)
                    else:
                        peer.command('serial.open', channel_id=1)
                    session_bytes = 0
                batch = report['completed_batches']
                seed = (args.seed + batch * 0x9e3779b9) & 0xffffffff
                report['in_progress_batch'] = dict(index=batch+1, seed=seed)
                save()
                result = peer.echo(args.byte_count, slow_read=(batch % 2 == 1), seed=seed)
                if result['bytes'] != args.byte_count:
                    raise RuntimeError(f'echo count mismatch: expected {args.byte_count}, observed {result["bytes"]}')
                session_bytes += args.byte_count
                status = peer.command('serial.status', channel_id=1)
                if (status.get('backend_rx_bytes') != session_bytes or status.get('backend_tx_bytes') != session_bytes
                        or status.get('pending_to_backend') != 0 or status.get('pending_to_peer') != 0):
                    raise RuntimeError(f'backend counters/queues mismatch: expected RX=TX={session_bytes}, observed {status!r}')
                sample = dict(batch=batch+1, **result)
                if measured:
                    last_diagnostics = diagnostics(peer,last_diagnostics)
                    if firmware == 'uart' and (last_diagnostics['uart_rx_overrun_events']
                            or last_diagnostics['uart_rx_lost_bytes_minimum']):
                        raise RuntimeError(f'UART loopback overrun/loss reported: {last_diagnostics!r}')
                    sample['diagnostics'] = last_diagnostics
                    report['last_diagnostics'] = last_diagnostics
                report['completed_batches'] += 1
                report['exact_bytes'] += args.byte_count
                if len(report['samples']) < MAX_SAMPLES:
                    report['samples'].append(sample)
                else:
                    report['omitted_samples'] += 1
                report['last_sample'] = sample
                report['last_backend_status'] = status
                del report['in_progress_batch']
                print(f'Batch {batch+1}: {result["bytes"]} exact bytes, {result["seconds"]:.3f}s, seed={seed}', flush=True)
                if (batch+1) % args.reconnect_every == 0:
                    peer.command('serial.close', channel_id=1)
                    peer.finish(); peer = None
                    report['completed_sessions'] += 1
                save()
            if peer is not None:
                peer.command('serial.close', channel_id=1)
                peer.finish(); peer = None
                report['completed_sessions'] += 1
            if not report['completed_batches']:
                raise RuntimeError('no byte-integrity batch completed')
            report['status'] = 'passed'
        except KeyboardInterrupt:
            report['status'] = 'interrupted'; report['error'] = 'user interrupted; incomplete run is not a pass'
        except (OSError, ValueError, RuntimeError) as exc:
            report['status'] = 'failed'; report['error'] = str(exc)
        finally:
            if peer is not None:
                peer.close() # Discard connection; no reset/retry after error.
            report['finished_utc'] = time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
            save()
        print(f'{report["status"]}: {report["exact_bytes"]} exact bytes, '
              f'{report["completed_batches"]} batches, {report["completed_sessions"]} closed sessions; {args.output}', flush=True)
        if 'error' in report:
            print('Failure:', report['error'], flush=True)
            rerun = [sys.executable, 'tools/dev.py', 'soak', '--board', args.board,
                     '--address', args.address, '--device-id', args.device_id,
                     '--firmware', firmware,
                     '--firmware-sha256', args.firmware_sha256, '--duration', str(args.duration),
                     '--byte-count', str(args.byte_count), '--batch-timeout', str(args.batch_timeout),
                     '--reconnect-every', str(args.reconnect_every), '--seed', hex(args.seed),
                     '--output', str(args.output) + '.rerun.json']
            if measured: rerun.append('--diagnostics')
            print('Rerun:', shlex.join(rerun), flush=True)
        return 0 if report['status'] == 'passed' else 1
