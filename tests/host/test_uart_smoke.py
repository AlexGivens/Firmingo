import pytest
from tools import uart_smoke


def channel(**changes):
    value = dict(id=1, backend='uart', controls=[], rx_queue=256, tx_queue=256,
                 configurable=['baud', 'data_bits', 'parity', 'stop_bits'],
                 baud_min=300, baud_max=2000000,
                 config=dict(baud=115200, actual_baud=115193, data_bits=8,
                             parity='none', stop_bits=1, flow_control='none'))
    value.update(changes)
    return value


def test_uart_capabilities_accept_explicit_supported_configuration():
    uart_smoke.validate_channel(channel())


def test_uart_capabilities_accept_configuration_persisted_from_prior_session():
    candidate = channel()
    candidate['config'].update(baud=57600, actual_baud=57597, data_bits=7,
                               parity='even', stop_bits=2)
    uart_smoke.validate_channel(candidate)


@pytest.mark.parametrize('changes', [
    dict(backend='application'), dict(controls=['rts']), dict(rx_queue=0),
    dict(configurable=[]), dict(baud_min=0), dict(baud_max=0), dict(config=None),
])
def test_uart_capabilities_reject_incompatible_shapes(changes):
    with pytest.raises(RuntimeError, match='capability mismatch'):
        uart_smoke.validate_channel(channel(**changes))


@pytest.mark.parametrize('key,value', [
    ('baud', 299), ('baud', 2000001), ('data_bits', 4), ('data_bits', 9),
    ('parity', 'mark'), ('stop_bits', 0), ('stop_bits', 3),
    ('flow_control', 'rts_cts'), ('actual_baud', 0),
])
def test_uart_capabilities_reject_invalid_current_configuration(key, value):
    candidate = channel()
    candidate['config'][key] = value
    with pytest.raises(RuntimeError, match='configuration mismatch|actual baud'):
        uart_smoke.validate_channel(candidate)
