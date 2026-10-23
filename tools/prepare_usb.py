"""Create a new local USB tree; never format disks or write to a chosen USB."""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path
import shutil
import tempfile
import zipfile


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest() if hasattr(hashlib, 'file_digest') else hashlib.sha256(stream.read()).hexdigest()


def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def safe_file(root, relative):
    item = Path(relative)
    if item.is_absolute() or '..' in item.parts or '\\' in relative or ':' in relative:
        raise ValueError('Unsafe manifest path')
    path = root / item
    if not path.is_file() or path.is_symlink() or not path.resolve().is_relative_to(root.resolve()):
        raise ValueError('Missing or unsafe file: ' + relative)
    return path


def collect_machine_profile():
    """Collect the Windows system partition identity via PowerShell.

    Returns a dict with keys: windows_esp_guid, windows_esp_partition,
    windows_esp_start_lba, windows_esp_size_lba.
    """
    esp_gpt_type = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
    ps = (
        "$d = (Get-Partition -DriveLetter C).DiskNumber; "
        "$p = Get-Partition -DiskNumber $d | Where-Object GptType -eq '" + esp_gpt_type + "'; "
        "if (-not $p) { exit 1 }; "
        "$g = $p.Guid -replace [char]123 -replace [char]125; "
        "$n = $p.PartitionNumber; "
        "$s = [math]::Floor($p.Offset / 512); "
        "$z = [math]::Floor($p.Size / 512); "
        "Write-Output \"windows_esp_guid=$g\"; "
        "Write-Output \"windows_esp_partition=$n\"; "
        "Write-Output \"windows_esp_start_lba=$s\"; "
        "Write-Output \"windows_esp_size_lba=$z\""
    )
    result = subprocess.run(
        ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', ps],
        capture_output=True, text=True, timeout=30,
    )
    if result.returncode != 0:
        raise ValueError('Failed to collect partition info: ' + result.stderr.strip())
    kv = {}
    for line in result.stdout.splitlines():
        line = line.strip()
        if '=' in line:
            k, v = line.split('=', 1)
            kv[k.strip()] = v.strip()
    for key in ('windows_esp_guid', 'windows_esp_partition',
                'windows_esp_start_lba', 'windows_esp_size_lba'):
        if key not in kv:
            raise ValueError('Missing partition field: ' + key)
    return kv


def write_machine_profile(kv, path):
    """Write the KV dict as a simple key=value text file."""
    lines = [f'{k}={v}' for k, v in kv.items()]
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def prepare(root, assets, output, autostart=False):
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ValueError('Choose a new output folder under an existing local parent')
    source = load(root / 'SOURCE-MANIFEST.json')
    files = []
    seen = set()
    for entry in source['files']:
        name = entry['path']
        path = safe_file(root, name)
        if name in seen or path.stat().st_size != entry['bytes'] or sha(path) != entry['sha256']:
            raise ValueError('Changed or duplicate public file: ' + name)
        seen.add(name)
        files.append((name, path))
    binary = safe_file(root, 'release/EFI/BOOT/BOOTX64.EFI')
    release = load(root / 'release/provenance.json')
    if sha(binary) != release['efi_sha256']:
        raise ValueError('EFI does not match successful-case hash')
    for entry in release['corresponding_sources']:
        if sha(safe_file(root, entry['path'])) != entry['sha256']:
            raise ValueError('EFI corresponding source mismatch')
    specs = load(root / 'unlock/assets.json')
    if len(specs) != 5 or len({x['Name'] for x in specs}) != 5:
        raise ValueError('Unexpected asset specification')
    checked_assets = []
    for entry in specs:
        path = safe_file(assets, entry['Name'])
        if path.stat().st_size != entry['Bytes'] or sha(path) != entry['Sha256']:
            raise ValueError('Wrong asset: ' + entry['Name'])
        checked_assets.append(path)
    # Stage only after every input has passed. Refuse existing destinations.
    with tempfile.TemporaryDirectory(prefix='.gpu-unlock-stage-', dir=output.parent) as temporary:
        stage = Path(temporary) / 'package'
        boot = stage / 'EFI/BOOT'
        payload = stage / 'RTX3090-UNLOCK'
        asset_out = payload / 'ASSETS'
        boot.mkdir(parents=True)
        asset_out.mkdir(parents=True)
        shutil.copyfile(binary, boot / 'BOOTX64.EFI')
        for path in checked_assets:
            shutil.copyfile(path, asset_out / path.name)
            if sha(asset_out / path.name) != sha(path):
                raise ValueError('Asset changed during copy')
        if sha(boot / 'BOOTX64.EFI') != release['efi_sha256']:
            raise ValueError('EFI changed during copy')
        shutil.copytree(root / 'LICENSES', payload / 'LICENSES')
        shutil.copyfile(root / 'LICENSE', payload / 'LICENSE')
        shutil.copyfile(root / 'THIRD_PARTY_NOTICES.md', payload / 'THIRD_PARTY_NOTICES.md')
        shutil.copytree(root / 'docs', payload / 'docs')
        if autostart:
            kv = collect_machine_profile()
            kv.setdefault('countdown_seconds', '5')
            write_machine_profile(kv, payload / 'machine_profile.txt')
            print('Collected machine profile:', kv)
        (stage / 'USB-README.md').write_text(
            '# Windows GPU UNLOCK\n\nRead RTX3090-UNLOCK/docs/GUIDE.ko.md before booting.\n'
            'Full public guide: https://github.com/impacifist/Windows-GPU-UNLOCK\n'
            'After unlock, use Boot Override -> Windows Boot Manager WITHOUT resetting.\n', encoding='utf-8')
        with zipfile.ZipFile(payload / 'SOURCE.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
            for name, path in files:
                if sha(path) != next(x['sha256'] for x in source['files'] if x['path'] == name):
                    raise ValueError('Source changed during packaging')
                archive.write(path, name)
            archive.write(root / 'SOURCE-MANIFEST.json', 'SOURCE-MANIFEST.json')
        if output.exists() or output.is_symlink():
            raise ValueError('Output appeared during preparation')
        stage.rename(output)
    print('Created local package:', output)
    print('Back up your FAT32 USB, then copy the contents manually. No disk was modified.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--autostart', action='store_true',
                        help='Collect local Windows partition info and include machine_profile.txt')
    args = parser.parse_args()
    prepare(Path(__file__).resolve().parents[1], args.assets.resolve(),
            args.output.absolute(), autostart=args.autostart)


if __name__ == '__main__':
    main()
