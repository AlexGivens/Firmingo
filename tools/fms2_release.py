"""Package an exact Nano FMS2 candidate build and its compiler handoff."""

import json
from pathlib import Path
import shutil
import tempfile

from experiments.managed_sketch.export_resident import COMMIT, bounded_file, git_blob, sha, unique_object
from experiments.managed_sketch.export_stateful import export_stateful
from tools.managed_profile import FMS2_PROFILE, FMS2_PROFILE_INPUTS
from tools.version import fms2_candidate_version

BOARD = "nano_rp2040_connect"


def export_candidate(profile_dir, output, commit, root):
    if not COMMIT.fullmatch(commit):
        raise ValueError("select an exact lowercase firmware commit")
    output = Path(output)
    if output.exists() or output.is_symlink():
        raise FileExistsError("select a new candidate output directory: " + str(output))
    profile_bytes = bounded_file(profile_dir / "profile-report.json", 256 * 1024)
    profile = json.loads(profile_bytes, object_pairs_hook=unique_object)
    version = fms2_candidate_version(git_blob(root, commit, "src/core/version.h"))
    expected = {"schema": 1, "board": BOARD, "firmware": FMS2_PROFILE, "firmware_version": version,
                "status": "compile-tested", "qualification": "release-candidate",
                "production_ready": False, "hardware_tested": False,
                "resident_report": "resident/report.json", "resident_abi": 2}
    if (not isinstance(profile, dict)
            or any(type(profile.get(key)) is not type(value) or profile[key] != value
                   for key, value in expected.items())):
        raise ValueError("build the Nano FMS2 release-candidate profile before exporting")
    inputs = {name: sha(git_blob(root, commit, name)) for name in FMS2_PROFILE_INPUTS}
    if profile.get("source_sha256") != inputs:
        raise ValueError("candidate build inputs differ from the selected firmware commit")
    if sha(Path(__file__).read_bytes()) != inputs["tools/fms2_release.py"]:
        raise ValueError("use the release exporter from the selected firmware commit")
    with tempfile.TemporaryDirectory(prefix=".fms2-release-", dir=output.parent) as temporary:
        stage = Path(temporary)
        compiler_dir = stage / "compiler-export"
        export_stateful(profile_dir / "resident", compiler_dir, commit, root, release_candidate=True)
        facts = json.loads((compiler_dir / "resident-facts.json").read_bytes())
        artifacts = {
            "resident_report_sha256": "build-report.json",
            "resident_elf_sha256": "resident.elf",
            "resident_uf2_sha256": "resident.uf2",
        }
        for key, filename in artifacts.items():
            if profile.get(key) != sha((compiler_dir / filename).read_bytes()):
                raise ValueError("candidate profile report differs from " + filename)
        compiler_manifest = (compiler_dir / "manifest.json").read_bytes()
        result = {
            "schema": 1, "purpose": "nano_fms2_release_candidate",
            "board": BOARD, "profile": facts["profile"], "buildProfile": FMS2_PROFILE,
            "firmwareCommit": commit, "firmwareVersion": version,
            "residentABI": facts["residentABI"], "uploadTargetID": facts["uploadTarget"]["id"],
            "residentUF2SHA256": facts["residentUF2SHA256"],
            "compilerContractSHA256": facts["compilerContractSHA256"],
            "compilerExport": {"path": "compiler-export/manifest.json", "bytes": len(compiler_manifest),
                               "sha256": sha(compiler_manifest)},
            "buildProfileReport": {"path": "profile-report.json", "bytes": len(profile_bytes),
                                   "sha256": sha(profile_bytes)},
            "buildProfileInputsSHA256": inputs,
            "hardwareQualified": False, "productionReady": False,
        }
        (stage / "profile-report.json").write_bytes(profile_bytes)
        (stage / "manifest.json").write_text(json.dumps(result, sort_keys=True, indent=2) + "\n",
                                            encoding="utf-8")
        output.mkdir()
        try:
            compiler_dir.rename(output / compiler_dir.name)
            (stage / "profile-report.json").rename(output / "profile-report.json")
            (stage / "manifest.json").rename(output / "manifest.json")
        except BaseException:
            shutil.rmtree(output)
            raise
    return result
