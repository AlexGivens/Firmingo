"""The USB overlay must never patch an installed core in place."""
from pathlib import Path
import hashlib
import json
import shutil
import pytest
from tools import usb_core

ROOT=Path(__file__).resolve().parents[2]


@pytest.fixture
def sdk(tmp_path):
    root=tmp_path/'repository'
    package=root/'patches/arduino-pico-6.0.0-usb-startup'
    shutil.copytree(ROOT/'patches/arduino-pico-6.0.0-usb-startup',package)
    data=tmp_path/'installed'
    core=data/'packages/rp2040/hardware/rp2040/6.0.0'
    shutil.copytree(ROOT/'third_party/arduino-pico-usb-startup/upstream',core)
    (core/'lib').mkdir()
    (core/'lib/dependency.a').write_bytes(b'read-only dependency fixture')
    (data/'packages/rp2040/tools').mkdir()
    (data/'packages/builtin').mkdir()
    (data/'package_index.json').write_text('{}')
    config={'directories':{'data':str(data),'downloads':str(tmp_path/'downloads'),'user':str(tmp_path/'user')}}
    return root,core,config,tmp_path/'overlay'


def test_valid_overlay_changes_only_real_copies_and_keeps_installed_files(sdk):
    root,core,config,output=sdk
    command,metadata=usb_core.prepare(root,['arduino-cli'],config,output)
    overlay=Path(metadata['overlay_path'])
    for name,hashes in metadata['manifest']['files'].items():
        assert not (overlay/name).is_symlink()
        assert usb_core.sha(core/name)==hashes['upstream_sha256']
        assert usb_core.sha(overlay/name)==hashes['patched_sha256']
    assert (overlay/'lib').is_symlink()
    assert (overlay/'lib/dependency.a').read_bytes()==b'read-only dependency fixture'
    generated=json.loads(Path(command[-1]).read_text())
    assert generated['directories']['data']==str((output/'data').resolve())
    assert generated['directories']['user']==config['directories']['user']


@pytest.mark.parametrize('fault',['dirty_core','bad_patch','unexpected_target'])
def test_unpinned_inputs_are_rejected_before_overlay_mutation(sdk,fault):
    root,core,config,output=sdk
    package=root/'patches/arduino-pico-6.0.0-usb-startup'
    manifest=json.loads((package/'manifest.json').read_text())
    if fault=='dirty_core':
        (core/'cores/rp2040/main.cpp').write_text('changed upstream')
    else:
        patch=package/'deferred-ncm.patch'
        patch.write_text(patch.read_text().replace('a/cores/rp2040/main.cpp','a/another.cpp',1))
        if fault=='unexpected_target':
            manifest['patch_sha256']=hashlib.sha256(patch.read_bytes()).hexdigest()
            (package/'manifest.json').write_text(json.dumps(manifest))
    with pytest.raises(RuntimeError):
        usb_core.prepare(root,['arduino-cli'],config,output)
    assert not output.exists()


@pytest.mark.parametrize('explicit', [False, True])
def test_cli_directory_defaults_are_queried_only_when_not_explicit(monkeypatch,explicit):
    from tools import dev
    calls=[]
    directories={'data':'/test/Arduino15','user':'/test/sketchbook','downloads':''}
    def run(command,**kwargs):
        calls.append(command)
        if command[2]=='dump':
            return json.dumps({'config':{'directories':dict(directories)} if explicit else {}})
        return json.dumps(directories[command[3].split('.')[-1]])
    monkeypatch.setattr(dev,'run',run)
    assert dev.effective_arduino_config(['arduino-cli'])['directories']==directories
    assert len(calls)==(1 if explicit else 4)
