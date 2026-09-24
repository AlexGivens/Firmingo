from concurrent.futures import ThreadPoolExecutor
from tools.smoke import echo, source


def test_source_records_have_exact_headers_sequence_and_payload(target):
    print(source(target))


def test_fragmented_full_duplex_echo_and_reconnect(target):
    print(echo(target))
    print(echo(target, byte_count=4096))


def test_slow_reader_while_source_is_active(target):
    with ThreadPoolExecutor(max_workers=2) as pool:
        stream = pool.submit(source, target)
        print(echo(target, slow_read=True))
        print(stream.result())
