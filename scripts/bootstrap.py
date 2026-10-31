"""Fetch pinned build inputs. No runtime network access or model dependencies."""
import argparse
import concurrent.futures
import hashlib
import io
import json
from pathlib import Path
import shutil
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / '.deps'
LO_COMMIT = '4f1df64b2e8e14751aa7c2205d170ca016071315'
LOCK = json.loads((ROOT / 'scripts/dependency-lock.json').read_text())

def verify_blob(blob, expected):
    if hashlib.sha256(blob).hexdigest() != expected:
        raise ValueError('Dependency checksum mismatch; no downloaded files were installed')


def download(url):
    req = urllib.request.Request(url, headers={'User-Agent': 'AutoKeyboard-build/0.1'})
    with urllib.request.urlopen(req, timeout=120) as response:
        return response.read()


def archive(url, target, kind, strip=True):
    expected = LOCK['archives'][target.name]
    if (target / '.complete').exists():
        if (target / '.complete').read_text().strip() != expected:
            raise ValueError(f'Unexpected cached dependency checksum: {target}')
        return
    print('Fetching', url, flush=True)
    blob = download(url)
    verify_blob(blob, expected)
    target.mkdir(parents=True, exist_ok=True)
    if kind == 'zip':
        with zipfile.ZipFile(io.BytesIO(blob)) as package:
            for item in package.infolist():
                parts = Path(item.filename).parts[1:] if strip else Path(item.filename).parts
                if not parts or item.is_dir():
                    continue
                out = target.joinpath(*parts).resolve()
                if not out.is_relative_to(target.resolve()):
                    raise ValueError('Unsafe archive path')
                out.parent.mkdir(parents=True, exist_ok=True)
                out.write_bytes(package.read(item))
    else:
        with tarfile.open(fileobj=io.BytesIO(blob), mode='r:gz') as package:
            for item in package.getmembers():
                if not item.isfile():
                    continue
                parts = Path(item.name).parts[1:] if strip else Path(item.name).parts
                if not parts:
                    continue
                out = target.joinpath(*parts).resolve()
                if not out.is_relative_to(target.resolve()):
                    raise ValueError('Unsafe archive path')
                out.parent.mkdir(parents=True, exist_ok=True)
                out.write_bytes(package.extractfile(item).read())
    (target / '.complete').write_text(hashlib.sha256(blob).hexdigest(), encoding='ascii')


def dictionaries():
    for language, base, names in [
        ('ar', 'ar', ['ar.aff', 'ar.dic', 'COPYING.txt', 'AUTHORS.txt', 'README_ar.txt']),
        ('en', 'en', ['en_US.aff', 'en_US.dic', 'license.txt', 'README_en_US.txt']),
    ]:
        target = ROOT / 'packs' / language
        target.mkdir(parents=True, exist_ok=True)
        source = target / 'SOURCE.txt'
        restore_original = source.exists() and 'removed dictionary roots' in source.read_text(encoding='utf-8')
        for name in names:
            out = target / name
            if not out.exists() or (restore_original and name.endswith('.dic')):
                blob=download(f'https://raw.githubusercontent.com/LibreOffice/dictionaries/{LO_COMMIT}/{base}/{name}')
                verify_blob(blob,LOCK['dictionaries'][f'{language}/{name}'])
                out.write_bytes(blob)
            verify_blob(out.read_bytes(),LOCK['dictionaries'][f'{language}/{name}'])
        dic = target / ('ar.dic' if language == 'ar' else 'en_US.dic')
        # Preserve short roots: the optional effective-bank length filter is
        # applied at lookup, after Hunspell expands affixes. This also lets the
        # user disable the filter without downloading dictionaries again.
        (target / 'SOURCE.txt').write_text(
            f'LibreOffice/dictionaries commit {LO_COMMIT}\n'
            'Original dictionary and affix rules; length filtering is a runtime setting.\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--arch', choices=['x64', 'x86'], default='x64')
    parser.add_argument('--toolchain', action='store_true', help='Fetch the portable LLVM-MinGW compiler (no SDK installation)')
    args = parser.parse_args()
    DEPS.mkdir(exist_ok=True)
    tasks = [
        ('https://github.com/hunspell/hunspell/archive/refs/tags/v1.7.2.tar.gz', DEPS / 'hunspell', 'tgz', True),
        ('https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip', DEPS / 'sqlite', 'zip', True),
        (f'https://github.com/bblanchon/pdfium-binaries/releases/download/chromium/8125/pdfium-win-{args.arch}.tgz', DEPS / f'pdfium-{args.arch}', 'tgz', False),
    ]
    if args.toolchain:
        toolchain = DEPS / 'llvm-mingw-20261020-ucrt-x86_64'
        if not (toolchain / 'bin' / 'clang.exe').exists():
            tasks.append(('https://github.com/mstorsjo/llvm-mingw/releases/download/20261020/llvm-mingw-20261020-ucrt-x86_64.zip', toolchain, 'zip', True))
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        jobs = [pool.submit(archive, *task) for task in tasks]
        jobs.append(pool.submit(dictionaries))
        for job in jobs:
            job.result()
    # Release bundles include all dependency license texts.
    licenses = DEPS / 'licenses'
    licenses.mkdir(exist_ok=True)
    for source, prefix in [(DEPS / 'hunspell', 'hunspell'), (DEPS / f'pdfium-{args.arch}', 'pdfium')]:
        for item in source.rglob('*'):
            if item.is_file() and (any(x in item.name.lower() for x in ['license', 'copying']) or 'licenses' in item.relative_to(source).parts):
                shutil.copy2(item, licenses / (prefix + '-' + '-'.join(item.relative_to(source).parts)))
    toolchain = DEPS / 'llvm-mingw-20261020-ucrt-x86_64'
    if toolchain.exists():
        shutil.copy2(toolchain / 'LICENSE.TXT', licenses / 'llvm-runtime-LICENSE.TXT')
        for item in (toolchain / 'x86_64-w64-mingw32/share/mingw32').glob('COPYING*'):
            shutil.copy2(item, licenses / ('mingw-runtime-' + item.name))
    print('Dependencies ready.', flush=True)


if __name__ == '__main__':
    main()
