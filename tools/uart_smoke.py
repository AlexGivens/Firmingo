"""Explicit-target Nano UART loopback validator; never flashes or edits routes."""
from tools.session_smoke import Probe
from tools.version import current_version

FIRMWARE_VERSION = current_version()
TEST_CONFIGURATION = dict(baud=115200, data_bits=8, parity='none', stop_bits=1,
                          flow_control='none')


def validate_channel(channel):
    if (channel.get('id') != 1 or channel.get('backend') != 'uart'
            or channel.get('controls') != []
            or channel.get('rx_queue') != 256 or channel.get('tx_queue') != 256
            or channel.get('configurable') != ['baud', 'data_bits', 'parity', 'stop_bits']
            or channel.get('baud_min') != 300 or channel.get('baud_max') != 2000000
            or not isinstance(channel.get('config'), dict)):
        raise RuntimeError(f'UART capability mismatch: {channel!r}')
    config = channel['config']
    baud = config.get('baud')
    if type(baud) is not int or not 300 <= baud <= 2000000:
        raise RuntimeError(f'UART current configuration mismatch: {config!r}')
    if (type(config.get('data_bits')) is not int
            or not 5 <= config['data_bits'] <= 8
            or config.get('parity') not in ('none', 'even', 'odd')
            or config.get('stop_bits') not in (1, 2)
            or config.get('flow_control') != 'none'):
        raise RuntimeError(f'UART current configuration mismatch: {config!r}')
    actual = config.get('actual_baud')
    if type(actual) is not int or actual <= 0:
        raise RuntimeError(f'UART actual baud is invalid: {config!r}')


def smoke(address, device_id, board, port=7420):
    options = dict(port=port, firmware_version=FIRMWARE_VERSION, backend='uart')
    with Probe(address, device_id, board, **options) as owner:
        validate_channel(owner.hello['channels'][0])
        print('Verified FMGO UART device:', owner.hello)
        owner.command('serial.open', channel_id=1, config=TEST_CONFIGURATION)
        with Probe(address, device_id, board, **options) as other:
            if other.hello['boot_id'] != owner.hello['boot_id']:
                raise RuntimeError('boot ID changed between concurrent sessions')
            other.command('serial.open', expected_error='busy', channel_id=1)
            print('Second-client BUSY: pass')
        print('Exact 115200 8N1 loopback:', owner.echo(16384))
        changed = dict(baud=57600, data_bits=8, parity='none', stop_bits=1,
                       flow_control='none')
        result = owner.command('serial.configure', channel_id=1, config=changed)
        for key, value in changed.items():
            if result.get(key) != value:
                raise RuntimeError(f'UART configuration acknowledgement mismatch: {result!r}')
        if type(result.get('actual_baud')) is not int or result['actual_baud'] <= 0:
            raise RuntimeError(f'UART actual baud is invalid: {result!r}')
        print('Exact 57600 8N1 loopback:', owner.echo(4096, slow_read=True))
        status = owner.command('serial.status', channel_id=1)
        if status.get('backend_rx_bytes') != 20480 or status.get('backend_tx_bytes') != 20480:
            raise RuntimeError(f'unexpected UART byte counters: {status!r}')
        diagnostics = owner.command('device.diagnostics')
        for field in ('uart_rx_overrun_events', 'uart_rx_lost_bytes_minimum',
                      'uart_tx_throttles'):
            if type(diagnostics.get(field)) is not int:
                raise RuntimeError(f'missing UART diagnostic {field}: {diagnostics!r}')
        if diagnostics['uart_rx_overrun_events'] or diagnostics['uart_rx_lost_bytes_minimum']:
            raise RuntimeError(f'UART loopback overrun/loss reported: {diagnostics!r}')
        print('UART counters and no-overrun diagnostics:', status)
        owner.command('serial.close', channel_id=1)
        owner.finish()
    return 0
