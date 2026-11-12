"""Read the firmware's single release version without a second hard-coded value."""

from pathlib import Path
import re


def fms2_candidate_version(header: bytes) -> str:
    """Read the separate Nano candidate identity from the selected version header."""
    match = re.search(rb'^#define FIRMINGO_FMS2_CANDIDATE_VERSION "([0-9]+\.[0-9]+\.[0-9]+-rc\.[1-9][0-9]*)"$',
                      header, re.MULTILINE)
    if not match:
        raise ValueError("src/core/version.h has no valid FMS2 release-candidate version")
    return match[1].decode("ascii")


def current_version(root: Path | None = None) -> str:
    root = root or Path(__file__).resolve().parents[1]
    header = (root / "src/core/version.h").read_text()
    match = re.search(r'^#define FIRMINGO_VERSION "(\d+\.\d+\.\d+)"$', header, re.M)
    if not match:
        raise RuntimeError("src/core/version.h has no valid FIRMINGO_VERSION")
    version = match.group(1)
    properties = (root / "library.properties").read_text()
    if f"version={version}\n" not in properties:
        raise RuntimeError("library.properties version differs from src/core/version.h")
    return version
