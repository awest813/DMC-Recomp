"""Package the Pages launcher and a checksum-pinned browser player release."""
import argparse
import hashlib
import json
import shutil
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NAMES = {'ps2web.html', 'ps2web.js', 'ps2web.wasm', 'ps2web.data'}
STATIC = ('index.html', 'style.css', 'launcher.mjs', 'files.mjs', 'service-worker.js', 'runtime.json')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-dir', type=Path, help='Use local build files instead of downloading the pinned release')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/pages')
    args = parser.parse_args()
    source = ROOT / 'web/pages'
    manifest = json.loads((source / 'runtime.json').read_text())
    if set(manifest['files']) != NAMES:
        raise ValueError('Runtime manifest must contain exactly the four player files')
    # Start with an empty directory so old states, disc files or unrelated output
    # can never ride along in a Pages artifact. Refuse to delete existing output.
    args.output.mkdir(parents=True, exist_ok=False)
    runtime = args.output / 'runtime'
    runtime.mkdir()
    for name, expected in manifest['files'].items():
        target = runtime / name
        if args.runtime_dir:
            shutil.copyfile(args.runtime_dir / name, target)
        else:
            url = f"https://github.com/awest813/DMC-Recomp/releases/download/{manifest['release']}/{name}"
            with urllib.request.urlopen(url, timeout=120) as response, target.open('wb') as output:
                shutil.copyfileobj(response, output)
        with target.open('rb') as data:
            digest = hashlib.file_digest(data, 'sha256').hexdigest()
        if target.stat().st_size != expected['bytes'] or digest != expected['sha256']:
            raise ValueError(f'Player checksum mismatch: {name}')
    for name in STATIC:
        shutil.copyfile(source / name, args.output / name)
    print(f'Pages ready: {args.output} (verified {manifest["release"]})')


if __name__ == '__main__':
    main()
