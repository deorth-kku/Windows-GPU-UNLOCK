"""Regenerate release/provenance.json and SOURCE-MANIFEST.json from current files."""
import hashlib
import json
from pathlib import Path


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(8192), b''):
            h.update(chunk)
    return h.hexdigest()


def main():
    root = Path(__file__).resolve().parents[1]

    # --- Update provenance.json ---
    prov_path = root / 'release' / 'provenance.json'
    with prov_path.open('r', encoding='utf-8') as f:
        prov = json.load(f)

    efi_path = root / 'release' / 'EFI' / 'BOOT' / 'BOOTX64.EFI'
    prov['efi_sha256'] = sha256(efi_path)
    prov['efi_bytes'] = efi_path.stat().st_size

    # Update existing source hashes
    for entry in prov['corresponding_sources']:
        p = root / entry['path']
        if p.exists():
            entry['sha256'] = sha256(p)
        else:
            print(f'WARNING: {entry["path"]} not found')

    # Add new source files not yet in the list
    existing = {e['path'] for e in prov['corresponding_sources']}
    for candidate in ['unlock/windows_boot.c', 'unlock/windows_boot.h']:
        p = root / candidate
        if p.exists() and candidate not in existing:
            prov['corresponding_sources'].append({'path': candidate, 'sha256': sha256(p)})
            print(f'Added {candidate}')

    prov['corresponding_sources'].sort(key=lambda x: x['path'])

    with prov_path.open('w', encoding='utf-8') as f:
        json.dump(prov, f, indent=2, ensure_ascii=False)
        f.write('\n')
    print(f'Updated {prov_path.name}: efi_sha256={prov["efi_sha256"][:16]}... sources={len(prov["corresponding_sources"])}')

    # --- Regenerate SOURCE-MANIFEST.json ---
    exclude_dirs = {
        'assets', 'benchmark/bin', 'benchmark/packages', 'benchmark/downloads',
        'benchmark/results', 'unlock/build', 'release/EFI', 'deps', '.git', 'docs',
        'usb-package',
    }
    exclude_files = {'SOURCE-MANIFEST.json'}

    files = []
    for p in sorted(root.rglob('*')):
        if not p.is_file():
            continue
        if p.suffix.lower() == '.md':
            continue
        rel = p.relative_to(root).as_posix()
        parts = p.relative_to(root).parts
        if any(part in exclude_dirs for part in parts):
            continue
        if rel in exclude_files:
            continue
        if rel == 'unlock/machine_profile.txt':
            continue
        files.append({'path': rel, 'bytes': p.stat().st_size, 'sha256': sha256(p)})

    manifest = {
        'project': 'Windows GPU UNLOCK',
        'format': 1,
        'note': 'Public source and release inventory; firmware, private fixtures and generated builds excluded.',
        'files': files,
    }

    manifest_path = root / 'SOURCE-MANIFEST.json'
    with manifest_path.open('w', encoding='utf-8') as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
        f.write('\n')
    print(f'Updated {manifest_path.name}: {len(files)} files')


if __name__ == '__main__':
    main()
