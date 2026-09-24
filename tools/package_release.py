"""Assemble exact-board beta UF2s from current, verified build reports."""

import json
from pathlib import Path
import shutil
import tempfile
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

from tools.version import current_version


SKETCH_NAMES = {
    "application": "firmingo_application",
    "uart": "firmingo_uart",
}


def collect(root: Path, board: str, profile: str) -> tuple[Path, dict]:
    from tools.dev import digest

    board_manifest = json.loads((root / "boards" / board / "baseline.json").read_text())
    if board_manifest.get("board") != board:
        raise RuntimeError(f"{board}: board manifest identity mismatch")
    build_dir = root / "build/firmware" / board / profile
    report_path = build_dir / "report.json"
    report = json.loads(report_path.read_text())
    if report.get("status") != "compile-tested" or report.get("firmware") != profile:
        raise RuntimeError(f"{board}/{profile}: successful current build required")
    if report.get("manifest") != board_manifest:
        raise RuntimeError(f"{board}/{profile}: build board settings differ from current manifest")
    sources = report.get("source_sha256")
    if not isinstance(sources, dict) or not sources:
        raise RuntimeError(f"{board}/{profile}: source hashes missing")
    for name, expected in sources.items():
        source = root / name
        if not source.is_file() or not source.resolve().is_relative_to(root.resolve()) or digest(source) != expected:
            raise RuntimeError(f"{board}/{profile}: source changed since build: {name}")
    for field, budget in (("flash_bytes", "flash_budget_bytes"),
                          ("static_ram_bytes", "static_ram_budget_bytes")):
        used = report.get(field)
        if type(used) is not int or used <= 0 or used > board_manifest[budget]:
            raise RuntimeError(f"{board}/{profile}: {field} is absent or exceeds budget")
    image = build_dir / "artifacts" / f"{SKETCH_NAMES[profile]}.ino.uf2"
    entry = report.get("artifacts", {}).get(image.name)
    if not isinstance(entry, dict) or not image.is_file():
        raise RuntimeError(f"{board}/{profile}: UF2 artifact missing")
    if image.stat().st_size != entry.get("bytes") or digest(image) != entry.get("sha256"):
        raise RuntimeError(f"{board}/{profile}: UF2 size or SHA-256 differs from build report")
    return image, report


def package(root: Path, boards: tuple[str, ...], profiles: tuple[str, ...]) -> Path:
    from tools.dev import BOARDS, digest

    version = current_version(root)
    if not boards or any(board not in BOARDS for board in boards) or len(set(boards)) != len(boards):
        raise RuntimeError("select distinct supported board IDs")
    if not profiles or any(profile not in SKETCH_NAMES for profile in profiles) or len(set(profiles)) != len(profiles):
        raise RuntimeError("select distinct release profiles")
    destination = root / "dist" / f"firmingo-{version}"
    archive_destination = Path(f"{destination}.zip")
    if destination.exists() or archive_destination.exists():
        raise RuntimeError(f"release package exists: {destination} or {archive_destination}; move it before repackaging")
    # Verify every input before writing any release file.
    inputs = [(board, profile, *collect(root, board, profile))
              for board in boards for profile in profiles]
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".firmingo-package-", dir=destination.parent) as temp:
        stage = Path(temp) / destination.name
        stage.mkdir()
        files = []
        for board, profile, image, report in inputs:
            relative = Path(board) / profile / f"firmingo-{version}-{board}-{profile}.uf2"
            target = stage / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(image, target)
            if digest(target) != report["artifacts"][image.name]["sha256"]:
                raise RuntimeError(f"{board}/{profile}: staged UF2 integrity failed")
            files.append({"path": relative.as_posix(), "board_id": board, "profile": profile,
                          "firmware_version": version, "bytes": target.stat().st_size,
                          "sha256": digest(target), "fqbn": report["manifest"]["fqbn"],
                          "flash_bytes": report["flash_bytes"],
                          "static_ram_bytes": report["static_ram_bytes"],
                          "qualification": "compile-tested",
                          "build_report_sha256": digest(root / "build/firmware" / board / profile / "report.json")})
        index = {"schema_version": 1, "product": "Firmingo", "version": version,
                 "release_channel": "beta", "artifact_format": "UF2", "files": files}
        (stage / "manifest.json").write_text(json.dumps(index, indent=2) + "\n")
        (stage / "README.md").write_text(
            f"# Firmingo {version} beta artifacts\n\n"
            "Select the UF2 whose `board_id` and `profile` match your hardware and intended backend. "
            "Verify `bytes` and `sha256` in `manifest.json` before installation. "
            "These images passed compilation and packaging checks only; hardware qualification "
            "of this exact build is not implied. See repository `docs/releasing.md` for release gates.\n")
        archive = Path(temp) / archive_destination.name
        with ZipFile(archive, "w") as bundle:
            for path in sorted(stage.rglob("*")):
                if path.is_file():
                    entry = ZipInfo(f"{destination.name}/{path.relative_to(stage).as_posix()}")
                    entry.date_time = (1980, 1, 1, 0, 0, 0)
                    entry.compress_type = ZIP_DEFLATED
                    entry.external_attr = 0o644 << 16
                    bundle.writestr(entry, path.read_bytes())
        stage.rename(destination)
        try:
            archive.rename(archive_destination)
        except OSError:
            shutil.rmtree(destination)
            raise
    return destination
