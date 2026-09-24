import pytest

from tools.uart_format_matrix import (assert_effective_configuration,
                                      configuration_command, format_cases,
                                      parse_peer_configuration)
from tools.uart_peer_smoke import fields, pattern, uart_config


def test_deterministic_peer_pattern_has_stable_vector():
    assert pattern(8, 0x12345678).hex() == '75cd254b84e2eaf2'
    assert pattern(8, 0x12345678) == pattern(8, 0x12345678)
    assert pattern(8, 0x12345678) != pattern(8, 0x12345679)


@pytest.mark.parametrize('count,seed', [(0, 1), (262145, 1), (1, -1), (1, 0x100000000)])
def test_peer_pattern_rejects_out_of_bounds(count, seed):
    with pytest.raises(ValueError):
        pattern(count, seed)


def test_peer_response_fields_ignore_non_field_tokens():
    assert fields('EVENT expect_done rx=17 mismatches=0') == {
        'rx': '17', 'mismatches': '0'}


@pytest.mark.parametrize('data_bits,maximum', [(5, 31), (6, 63), (7, 127), (8, 255)])
def test_deterministic_peer_pattern_respects_uart_data_width(data_bits, maximum):
    value = pattern(1024, 0x87654321, data_bits)
    assert max(value) <= maximum
    assert value == bytes(byte & maximum for byte in pattern(1024, 0x87654321))


def test_uart_format_matrix_covers_every_advertised_shape_once():
    cases = format_cases()
    shapes = {(case['data_bits'], case['parity'], case['stop_bits']) for case in cases}
    assert len(cases) == len(shapes) == 24
    assert {case['baud'] for case in cases} == {57600}
    assert {shape[0] for shape in shapes} == {5, 6, 7, 8}
    assert {shape[1] for shape in shapes} == {'none', 'even', 'odd'}
    assert {shape[2] for shape in shapes} == {1, 2}


def test_peer_configuration_command_and_response_are_explicit():
    config = uart_config(38400, 7, 'even', 2)
    assert configuration_command(config) == 'CONFIG 38400 7 even 2'
    assert parse_peer_configuration(
        'OK baud=38400 actual_baud=38397 data_bits=7 parity=even stop_bits=2') == {
            'baud': 38400, 'actual_baud': 38397, 'data_bits': 7,
            'parity': 'even', 'stop_bits': 2}


@pytest.mark.parametrize('line', [
    'OK baud=299 actual_baud=299 data_bits=8 parity=none stop_bits=1',
    'OK baud=115200 actual_baud=0 data_bits=8 parity=none stop_bits=1',
    'OK baud=115200 actual_baud=115207 data_bits=9 parity=none stop_bits=1',
    'OK baud=115200 actual_baud=115207 data_bits=8 parity=mark stop_bits=1',
    'OK baud=115200 actual_baud=115207 data_bits=8 parity=none stop_bits=3',
    'OK baud=x actual_baud=115207 data_bits=8 parity=none stop_bits=1',
])
def test_invalid_peer_configuration_responses_are_rejected(line):
    with pytest.raises(RuntimeError):
        parse_peer_configuration(line)


def test_nano_effective_configuration_requires_exact_shape_and_actual_baud():
    config = uart_config(115200, 8, 'odd', 2)
    assert_effective_configuration(dict(config, actual_baud=115207), config, 115207)
    with pytest.raises(RuntimeError):
        assert_effective_configuration(dict(config, actual_baud=115200), config, 115207)
