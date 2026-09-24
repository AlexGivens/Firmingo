"""Release input gates and output metadata; synthetic bytes are never flashable."""

import json

import pytest
from zipfile import ZipFile

from tools.dev import digest
from tools.package_release import package


@pytest.fixture
def release_tree(tmp_path):
    (tmp_path / "src/core").mkdir(parents=True)
    (tmp_path / "src/core/version.h").write_text('#define FIRMINGO_VERSION "0.1.0"\n')
    (tmp_path / "library.properties").write_text('version=0.1.0\n')
    source = tmp_path / "source.cpp"
    source.write_text("initial source")
    for board in ("nano_rp2040_connect", "raspberry_pi_pico"):
        board_dir = tmp_path / "boards" / board
        board_dir.mkdir(parents=True)
        manifest = dict(board=board, fqbn="rp2040:rp2040:" + board,
                        flash_budget_bytes=1000, static_ram_budget_bytes=500)
        (board_dir / "baseline.json").write_text(json.dumps(manifest))
        output = tmp_path / "build/firmware" / board / "application"
        (output / "artifacts").mkdir(parents=True)
        image = output / "artifacts/firmingo_application.ino.uf2"
        image.write_bytes(("synthetic-" + board).encode())
        report = dict(status="compile-tested", firmware="application", manifest=manifest,
                      source_sha256={"source.cpp": digest(source),
                                     "src/core/version.h": digest(tmp_path / "src/core/version.h")},
                      flash_bytes=200, static_ram_bytes=100,
                      artifacts={image.name: dict(bytes=image.stat().st_size, sha256=digest(image))})
        (output / "report.json").write_text(json.dumps(report))
    return tmp_path, source


def test_package_names_and_hashes_each_board_image(release_tree):
    root, _ = release_tree
    destination = package(root, ("nano_rp2040_connect", "raspberry_pi_pico"), ("application",))
    index = json.loads((destination / "manifest.json").read_text())
    assert index["version"] == "0.1.0" and index["release_channel"] == "beta"
    assert len(index["files"]) == 2
    for item in index["files"]:
        image = destination / item["path"]
        assert item["board_id"] in item["path"]
        assert item["bytes"] == image.stat().st_size
        assert item["sha256"] == digest(image)
        assert item["qualification"] == "compile-tested"
    with ZipFile(destination.parent / f"{destination.name}.zip") as bundle:
        assert json.loads(bundle.read("firmingo-0.1.0/manifest.json")) == index
        for item in index["files"]:
            assert bundle.read("firmingo-0.1.0/" + item["path"]) == (destination / item["path"]).read_bytes()
    with pytest.raises(RuntimeError, match="exists"):
        package(root, ("nano_rp2040_connect",), ("application",))


@pytest.mark.parametrize("fault", ["source", "image", "board", "budget", "failed"])
def test_bad_input_never_creates_release(release_tree, fault):
    root, source = release_tree
    board = "raspberry_pi_pico"
    output = root / "build/firmware" / board / "application"
    report_path = output / "report.json"
    report = json.loads(report_path.read_text())
    if fault == "source":
        source.write_text("changed")
    elif fault == "image":
        (output / "artifacts/firmingo_application.ino.uf2").write_bytes(b"changed")
    elif fault == "board":
        report["manifest"]["fqbn"] = "wrong"
    elif fault == "budget":
        report["flash_bytes"] = 1001
    else:
        report["status"] = "failed"
    report_path.write_text(json.dumps(report))
    with pytest.raises(RuntimeError):
        package(root, ("nano_rp2040_connect", "raspberry_pi_pico"), ("application",))
    assert not (root / "dist/firmingo-0.1.0").exists()
