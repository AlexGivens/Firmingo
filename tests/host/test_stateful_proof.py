"""Independent, non-executable ELF metadata fixtures for candidate FMS2 gates."""

import hashlib
import struct

import pytest

from experiments.managed_sketch.build_stateful_proof import Elf, module_capsule, resident_contract


def fixture(sections, symbols, loads):
    """Hand-author ELF32 metadata; dummy instructions must never be deployed."""
    names = bytearray(b"\0")
    symbol_bytes = bytearray(16)
    for name, value, size, info, index in symbols:
        name_offset = len(names)
        names.extend(name.encode() + b"\0")
        symbol_bytes.extend(struct.pack("<IIIBBH", name_offset, value, size, info, 0, index))
    count = len(sections)
    sections = [*sections, (".symtab", 2, 0, 0, bytes(symbol_bytes), count + 2, 16),
                (".strtab", 3, 0, 0, bytes(names), 0, 0)]
    section_names = b"\0" + b"\0".join(s[0].encode() for s in sections) + b"\0.shstrtab\0"
    sections.append((".shstrtab", 3, 0, 0, section_names, 0, 0))
    data = bytearray(256)
    headers = [(0,) * 10]
    for name, kind, flags, address, payload, link, entry in sections:
        offset = len(data)
        size = payload if isinstance(payload, int) else len(payload)
        if kind != 8:
            data.extend(payload)
        headers.append((section_names.index(name.encode() + b"\0"), kind, flags,
                        address, offset, size, link, 0, 4, entry))
        data.extend(bytes(-len(data) % 4))
    shoff = len(data)
    for header in headers:
        data.extend(struct.pack("<10I", *header))
    struct.pack_into("<16sHHIIIIIHHHHHH", data, 0, b"\x7fELF\x01\x01\x01" + bytes(9),
                     2, 40, 1, 0, 52, shoff, 0x05000200, 52, 32, len(loads), 40, len(headers), len(headers) - 1)
    for index, (section_index, physical, size) in enumerate(loads):
        h = headers[section_index]
        struct.pack_into("<8I", data, 52 + index * 32, 1, h[4], h[3], physical, size, size, 7, 4)
    return bytes(data)


def resident_fixture(*, arena_type=8, alias=False, version=2, state=0x20002000, heap=0x20040000, boot_mode=None, upload=False, checkpoint=None):
    calls = [0x10000041] * 8
    rodata = struct.pack("<IHH8I", 0x49504146, 1, 40, *calls)
    rodata += struct.pack("<IHH11I", 0x49504146, version, 52, *calls, state, 4096, 4096)
    if boot_mode is not None:
        rodata += struct.pack("<I", boot_mode)
    upload_address = 0x10000080 + len(rodata)
    if upload:
        rodata += struct.pack("<IHH4I16s16s", 0x32505546, 1, 56, 2, 4096, 1024, 30000,
                              b"nano-managed-v2", b"physical-d2-gnd")
        rodata += b"0.1.0-statefulupload-exp1\0"
    if checkpoint is not None:
        rodata += bytes(-len(rodata) % 4)
    checkpoint_address = 0x10000080 + len(rodata)
    if checkpoint is not None:
        rodata += struct.pack("<4I", *checkpoint)
    rodata += bytes((256 if upload or checkpoint is not None else 128) - len(rodata))
    sections = [(".text", 1, 6, 0x10000000, bytes(128), 0, 0),
                (".rodata", 1, 2, 0x10000080, rodata, 0, 0),
                (".bss", arena_type, 3, 0x20002000, 4096 if arena_type == 8 else bytes(4096), 0, 0)]
    symbols = [("firmingo_sketch_api", 0x10000080, 40, 0x11, 2),
               ("firmingo_sketch_api_v2", 0x100000a8, 52, 0x11, 2),
               ("firmingo_sketch_state", 0x20002000, 4096, 0x11, 3)]
    for name, value in {"__bss_start__": 0x20002000, "__bss_end__": 0x20003000,
                        "__end__": 0x20003000, "__HeapLimit": heap,
                        "__StackOneBottom": 0x20040800, "__StackOneTop": 0x20041000,
                        "__StackBottom": 0x20041800, "__StackTop": 0x20042000}.items():
        symbols.append((name, value, 0, 0x10, 0xfff1))
    if alias:
        symbols.append(("other_owner", 0x20002004, 4, 0x11, 3))
    if boot_mode is not None:
        symbols.append(("firmingo_stateful_boot_mode", 0x100000dc, 4, 0x11, 2))
    if upload:
        symbols.append(("firmingo_stateful_upload_contract", upload_address, 56, 0x11, 2))
    if checkpoint is not None:
        symbols.append(("firmingo_flash_checkpoint_contract", checkpoint_address, 16, 0x11, 2))
    return fixture(sections, symbols, [(1, 0x10000000, 128 + len(rodata))])


def module_fixture(*, extra=False, bss=4, data_base=0x20002000, load=0x10200108, entry=0x10200101):
    sections = [(".text", 1, 6, 0x10200100, bytes.fromhex("00bf704700bf7047"), 0, 0),
                (".data", 1, 3, data_base, struct.pack("<I", 7), 0, 0),
                (".bss", 8, 3, data_base + 4, bss, 0, 0)]
    if extra:
        sections.append((".init_array", 1, 3, data_base + 8, bytes(4), 0, 0))
    symbols = [("firmingo_sketch_setup", entry, 4, 0x12, 1),
               ("firmingo_sketch_loop", 0x10200105, 4, 0x12, 1)]
    return fixture(sections, symbols, [(1, 0x10200100, 8), (2, load, 4)])


def test_actual_table_bytes_bss_and_linker_bounds_define_candidate_contract():
    contract, flash = resident_contract(resident_fixture())
    assert contract["apiAddress"] == 0x100000a8
    assert contract["stateAddress"] == 0x20002000 and contract["stateCapacityBytes"] == 4096
    assert not contract["runtimeEnabled"] and not contract["hardwareTested"]
    assert len(flash) == 1 and len(flash[0][1]) == 256


def test_boot_runtime_is_distinguished_by_actual_readonly_marker_not_report_claims():
    contract, _ = resident_contract(resident_fixture(boot_mode=1))
    assert contract["runtimeEnabled"] and not contract["networkUploadsEnabled"]
    assert not contract["fms2NetworkUploadsEnabled"] and not contract["hardwareTested"]


@pytest.mark.parametrize("mode", [0, 2])
def test_unknown_boot_mode_marker_rejects(mode):
    with pytest.raises(ValueError, match="runtime marker"):
        resident_contract(resident_fixture(boot_mode=mode))


@pytest.mark.parametrize("fault", [{"version": 1}, {"arena_type": 1}, {"alias": True},
                                   {"state": 0x20002004}, {"heap": 0x20002fff}])
def test_mismatched_table_or_unowned_ram_rejects(fault):
    with pytest.raises(ValueError):
        resident_contract(resident_fixture(**fault))


def test_linked_sections_produce_exact_image_metadata_and_independent_hashes():
    contract, _ = resident_contract(resident_fixture())
    capsule, sections = module_capsule(module_fixture(), contract)
    assert sections["dataBytes"] == 4 and sections["bssBytes"] == 4
    assert len(capsule) == 268 and capsule[:4] == b"FMS2"
    assert struct.unpack_from("<4I", capsule, 32) == (0x20002000, 4, 0x20002004, 4)
    assert capsule[64:96] == hashlib.sha256(capsule[256:]).digest()
    assert capsule[96:128] == hashlib.sha256(capsule[:96]).digest()
    assert capsule[-4:] == struct.pack("<I", 7)


@pytest.mark.parametrize("fault", [{"extra": True}, {"bss": 4096}, {"bss": 3},
                                   {"data_base": 0x20002004}, {"load": 0x10200104},
                                   {"entry": 0x10200109}])
def test_unsupported_sections_overflow_wrong_ram_loads_and_entries_reject(fault):
    contract, _ = resident_contract(resident_fixture())
    with pytest.raises(ValueError):
        module_capsule(module_fixture(**fault), contract)


def test_elf_metadata_truncation_rejects_before_reading_outside_file():
    data = module_fixture()
    for length in range(len(data)):
        with pytest.raises((ValueError, UnicodeError)):
            Elf(data[:length])


def test_missing_tool_records_failed_proof_without_success_claim(tmp_path, monkeypatch):
    import json
    import sys
    from experiments.managed_sketch.build_stateful_proof import main
    output = tmp_path / "new-proof"
    monkeypatch.setattr(sys, "argv", ["proof", "--cli", str(tmp_path / "absent-cli"),
                                    "--config-file", str(tmp_path / "absent.yaml"),
                                    "--validator", str(tmp_path / "absent-checker"),
                                    "--output-dir", str(output)])
    with pytest.raises(ValueError, match="missing"):
        main()
    report = json.loads((output / "report.json").read_text())
    assert report["status"] == "failed" and "error" in report
    assert not report["runtimeEnabled"] and not report["hardwareTested"]
    assert not (output / "linker-contract.json").exists()


def test_existing_proof_output_is_never_overwritten(tmp_path, monkeypatch):
    import sys
    from experiments.managed_sketch.build_stateful_proof import main
    output = tmp_path / "existing"
    output.mkdir()
    receipt = output / "report.json"
    receipt.write_text("previous evidence")
    monkeypatch.setattr(sys, "argv", ["proof", "--cli", "unused", "--config-file", "unused",
                                    "--validator", "unused", "--output-dir", str(output)])
    with pytest.raises(FileExistsError):
        main()
    assert receipt.read_text() == "previous evidence"


@pytest.mark.parametrize('kind,phase', [(1, 'erase'), (2, 'first-page')])
def test_fault_checkpoint_facts_come_from_actual_mapped_marker(kind, phase):
    contract, _ = resident_contract(resident_fixture(boot_mode=1, upload=True,
        checkpoint=(0x31504346, 1, kind, 60000)))
    assert contract['flashCheckpoint'] == dict(after=phase, hold_ms=60000,
        expiry='fail without continuing', chip_busy_interruption=False)
    normal, _ = resident_contract(resident_fixture(boot_mode=1, upload=True))
    assert 'flashCheckpoint' not in normal


@pytest.mark.parametrize('marker', [(0, 1, 1, 60000), (0x31504346, 2, 1, 60000),
    (0x31504346, 1, 0, 60000), (0x31504346, 1, 3, 60000), (0x31504346, 1, 1, 0)])
def test_unknown_fault_marker_never_acquires_normal_resident_meaning(marker):
    with pytest.raises(ValueError, match='flash checkpoint'):
        resident_contract(resident_fixture(boot_mode=1, upload=True, checkpoint=marker))


@pytest.mark.parametrize('script,mode', [('build_resident', []), ('build_stateful_proof', []),
    ('build_stateful_proof', ['--boot-runtime'])])
def test_fault_mode_requires_upload_before_acquiring_tools_or_creating_outputs(tmp_path, monkeypatch, script, mode):
    import importlib
    import sys
    output = tmp_path / 'must-not-exist'
    arguments = [script, '--cli', 'absent', '--config-file', 'absent', '--output-dir', str(output),
                 '--flash-checkpoint', 'erase', *mode]
    if script == 'build_stateful_proof':
        arguments += ['--validator', 'absent']
    monkeypatch.setattr(sys, 'argv', arguments)
    with pytest.raises(SystemExit) as error:
        importlib.import_module('experiments.managed_sketch.' + script).main()
    assert error.value.code == 2
    assert not output.exists()
