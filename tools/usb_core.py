"""Create a pinned USB-startup build overlay without modifying an installed core."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(root, cli, config, destination):
    """Read-only installed dependencies, real copies of every patched file."""
    package = root / 'patches/arduino-pico-6.0.0-usb-startup'
    manifest = json.loads((package / 'manifest.json').read_text())
    patch = package / manifest['patch']
    if sha(patch) != manifest['patch_sha256']:
        raise RuntimeError('USB startup patch hash changed; review and update its manifest')
    data = Path(config['directories']['data'])
    core = data / 'packages/rp2040/hardware/rp2040' / manifest['core_version']
    for name, hashes in manifest['files'].items():
        if sha(core / name) != hashes['upstream_sha256']:
            raise RuntimeError(f'USB startup requires clean pinned upstream file: {core / name}')
    # Reject unexpected diff targets before invoking patch. All allowed targets
    # become real files in the overlay, so patch cannot follow a dependency link.
    old_targets = [line[4:] for line in patch.read_text().splitlines() if line.startswith('--- ')]
    new_targets = [line[4:] for line in patch.read_text().splitlines() if line.startswith('+++ ')]
    if old_targets != ['a/' + n for n in manifest['files']] or new_targets != ['b/' + n for n in manifest['files']]:
        raise RuntimeError('USB patch target list differs from pinned manifest')
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    staged_data = destination / 'data'
    staged_data.mkdir()
    for entry in data.iterdir():
        if entry.name != 'packages':
            (staged_data / entry.name).symlink_to(entry.resolve(), target_is_directory=entry.is_dir())
    packages = staged_data / 'packages'
    packages.mkdir()
    for entry in (data / 'packages').iterdir():
        if entry.name != 'rp2040':
            (packages / entry.name).symlink_to(entry.resolve(), target_is_directory=entry.is_dir())
    rp = packages / 'rp2040'
    rp.mkdir()
    for entry in (data / 'packages/rp2040').iterdir():
        if entry.name != 'hardware':
            (rp / entry.name).symlink_to(entry.resolve(), target_is_directory=entry.is_dir())
    overlay = rp / 'hardware/rp2040' / manifest['core_version']
    mirror(core, overlay, set(manifest['files']))
    command = ['patch', '--batch', '--fuzz=0', '--no-backup-if-mismatch', '-p1', '-i', str(patch.resolve())]
    print('+ ' + ' '.join(command), flush=True)
    try:
        subprocess.run(command, cwd=overlay, check=True, timeout=10)
    except subprocess.CalledProcessError as exc:
        raise RuntimeError("USB startup patch failed in the build overlay; installed sources were not patched") from exc
    for name, hashes in manifest['files'].items():
        if sha(overlay / name) != hashes['patched_sha256']:
            raise RuntimeError(f'patched USB file hash mismatch: {name}')
        if sha(core / name) != hashes['upstream_sha256']:
            raise RuntimeError(f'installed core unexpectedly changed: {name}')
    generated = dict(config)
    generated['directories'] = dict(config['directories'], data=str(staged_data.resolve()))
    path = destination / 'arduino-cli.yaml'
    # JSON is valid YAML; retain upstream CLI settings without a YAML dependency.
    path.write_text(json.dumps(generated, indent=2) + '\n')
    metadata = {'core_path': str(core.resolve()), 'overlay_path': str(overlay.resolve()),
                'manifest': manifest}
    return [cli[0], '--config-file', str(path.resolve())], metadata


def mirror(source, destination, modified):
    destination.mkdir(parents=True)
    for entry in source.iterdir():
        target = destination / entry.name
        if entry.name in modified:
            shutil.copyfile(entry, target)
        elif any(n.startswith(entry.name + '/') for n in modified):
            mirror(entry, target, {n[len(entry.name)+1:] for n in modified if n.startswith(entry.name + '/')})
        else:
            target.symlink_to(entry.resolve(), target_is_directory=entry.is_dir())
