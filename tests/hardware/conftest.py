import pytest
from tools.dev import BOARDS
from tools.smoke import validate_target, identity


def pytest_addoption(parser):
    parser.addoption("--board", choices=BOARDS)
    parser.addoption("--address")
    parser.addoption("--device-id", help="Expected 16-hex-digit board ID from verified diagnostics")
    parser.addoption("--allow-legacy-no-identity", action="store_true",
                     help="Acknowledge the prototype cannot report a stable device ID")


@pytest.fixture(scope="session")
def target(pytestconfig):
    board = pytestconfig.getoption("--board")
    address = pytestconfig.getoption("--address")
    if not board or not address:
        pytest.skip("hardware not selected; use tools/dev.py smoke --help")
    selected = validate_target(address)
    device_id = pytestconfig.getoption("--device-id")
    if device_id:
        print("Verified device:", identity(selected, device_id, board))
        return selected
    if not pytestconfig.getoption("--allow-legacy-no-identity"):
        pytest.fail("select --device-id, or explicitly acknowledge --allow-legacy-no-identity for the old prototype")
    return validate_target(address)
