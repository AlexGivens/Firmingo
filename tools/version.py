"""Read the firmware's single release version without a second hard-coded value."""

from pathlib import Path
import re


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
