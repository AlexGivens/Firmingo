"""Versioned FMS2 exports use owned non-executable ELF/UF2/source fixtures."""
import hashlib
import json
from pathlib import Path
import struct

import pytest
from experiments.managed_sketch.export_stateful import export_stateful, RESOURCES, REQUIRED, VALIDATORS, validate_layout
from experiments.managed_sketch.build_stateful_proof import resident_contract
from test_stateful_proof import resident_fixture
from test_resident_export import git

ROOT = Path(__file__).resolve().parents[2]
sha = lambda data: hashlib.sha256(data).hexdigest()


@pytest.fixture
def selected(tmp_path):
    root=tmp_path/'source';root.mkdir();git(root,'init','-b','main')
    sources={name:(ROOT/name).read_bytes() for name in REQUIRED}
    files={**sources,**{n:(ROOT/n).read_bytes() for n in {*RESOURCES.values(),*VALIDATORS}}}
    for name,data in files.items():
        path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
    git(root,'add','.');git(root,'-c','user.name=Fixture','-c','user.email=fixture@example.invalid','-c','commit.gpgsign=false','commit','-m','non-executable FMS2 source fixture')
    commit=git(root,'rev-parse','HEAD'); resident=tmp_path/'resident'; (resident/'artifacts').mkdir(parents=True)
    elf=resident_fixture(boot_mode=1,upload=True);facts,flash=resident_contract(elf)
    payload=b''.join(data for _,data in flash);payload+=bytes([0xff])*(-len(payload)%256);blocks=[]
    for index in range(len(payload)//256):
        block=bytearray(512);struct.pack_into('<8I',block,0,0x0a324655,0x9e5d5157,0x2000,0x10000000+index*256,256,index,len(payload)//256,0xe48bff56)
        block[32:288]=payload[index*256:(index+1)*256];struct.pack_into('<I',block,508,0x0ab16f30);blocks.append(block)
    uf2=b''.join(blocks)
    for extension,data in [('elf',elf),('uf2',uf2)]: (resident/'artifacts'/('firmingo_managed_resident.ino.'+extension)).write_bytes(data)
    report={'status':'compile-tested FMS2 upload candidate; not flashed','board':'nano_rp2040_connect','core':'rp2040:rp2040@6.0.0',
            'fqbn':json.loads(sources['boards/nano_rp2040_connect/baseline.json'])['fqbn'],
            'stateful_runtime_enabled':True,'network_uploads_enabled':True,
            'upload_target':dict(id=2,format='nano-managed-v2',max_size=4096,max_chunk=1024,authorization='physical-d2-gnd'),
            'flash_bytes':384,'flash_budget_bytes':1048576,'static_ram_bytes':12288,'static_ram_budget_bytes':196608,
            'elf_sha256':sha(elf),'uf2_sha256':sha(uf2),'source_sha256':{n:sha(d) for n,d in sources.items()}}
    (resident/'report.json').write_text(json.dumps(report))
    return root,resident,commit,report


def test_export_checks_elf_uf2_sources_resources_and_versions_without_recipe_or_device(selected,tmp_path):
    root,resident,commit,_=selected; out=tmp_path/'export';result=export_stateful(resident,out,commit,root)
    contract=json.loads((out/'contract.json').read_text());facts=json.loads((out/'resident-facts.json').read_text());manifest=json.loads((out/'manifest.json').read_text())
    assert contract['identifier']=='firmingo-nano-managed-abi2-v1-experimental'
    assert contract['upload']['targetID']==2 and contract['residentABI']==2 and contract['image']['version']==2
    assert facts['stateAddress']==0x20002000 and facts['uploadTarget']['stagingTimeoutMs']==30000
    assert facts['compilerContractSHA256']==sha((out/'contract.json').read_bytes())
    assert facts['elfUF2ComparedBytes']==384 and not facts['hardwareTested']
    assert manifest['schema']==3 and not manifest['hardwareQualified']
    assert result['manifestSHA256']==sha((out/'manifest.json').read_bytes())
    assert 'recipeIdentifier' not in contract and 'deviceID' not in facts
    for f in manifest['files']: assert sha((out/f['path']).read_bytes())==f['sha256']
    second=tmp_path/'second';export_stateful(resident,second,commit,root)
    assert (out/'manifest.json').read_bytes()==(second/'manifest.json').read_bytes()
    with pytest.raises(FileExistsError): export_stateful(resident,out,commit,root)


@pytest.mark.parametrize('fault',['status','target','source','omitted','uf2','elf','ram','budget','path'])
def test_wrong_builds_or_corrupt_artifacts_reject_before_output(selected,tmp_path,fault):
    root,resident,commit,r=selected
    if fault=='status':r['status']='compile-tested FMS2 boot runtime candidate; network uploads disabled; not flashed'
    elif fault=='target':r['upload_target']['id']=1
    elif fault=='source':r['source_sha256']['experiments/managed_sketch/stateful_upload.cpp']='0'*64
    elif fault=='omitted':r['source_sha256'].pop('experiments/managed_sketch/stateful_upload.cpp')
    elif fault=='ram':r['static_ram_bytes']=8192
    elif fault=='budget':r['static_ram_bytes']=True
    elif fault=='path':r['source_sha256']['../escape']='0'*64
    else:
        p=resident/'artifacts'/('firmingo_managed_resident.ino.'+fault);d=bytearray(p.read_bytes());d[32 if fault=='uf2' else 256]^=1;p.write_bytes(d);r[fault+'_sha256']=sha(d)
    (resident/'report.json').write_text(json.dumps(r));out=tmp_path/'rejected'
    with pytest.raises((ValueError,OSError)):export_stateful(resident,out,commit,root)
    assert not out.exists()


def test_layout_fields_and_hash_spans_match_independent_fms2_fixture():
    layout=json.loads((ROOT/'experiments/managed_sketch/stateful_contract.json').read_text())
    data=bytes.fromhex((ROOT/'tests/fixtures/managed-stateful-v2/initialized.hex').read_text())
    facts,_=resident_contract(resident_fixture(boot_mode=1,upload=True));validate_layout(layout,facts)
    for field in layout['image']['fields']:
        raw=data[field['offset']:field['offset']+field['bytes']]
        if field['name']=='payloadSHA256':assert raw==hashlib.sha256(data[256:]).digest()
        if field['name']=='headerSHA256':assert raw==hashlib.sha256(data[:96]).digest()
        if field['kind']=='zero':assert not any(raw)
    layout['image']['fields'][3]['offset']=9
    with pytest.raises(ValueError,match='offsets'):validate_layout(layout,facts)


def test_fault_resident_cannot_be_exported_even_with_normal_report_status(selected, tmp_path):
    root, resident, commit, report = selected
    elf = resident_fixture(boot_mode=1, upload=True, checkpoint=(0x31504346, 1, 1, 60000))
    (resident / 'artifacts/firmingo_managed_resident.ino.elf').write_bytes(elf)
    report['elf_sha256'] = sha(elf)
    (resident / 'report.json').write_text(json.dumps(report))
    with pytest.raises(ValueError, match='fault images cannot be exported'):
        export_stateful(resident, tmp_path / 'no-export', commit, root)
    assert not (tmp_path / 'no-export').exists()
