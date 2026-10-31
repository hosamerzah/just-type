"""Deterministic source/build identity and staged-file integrity checks."""
from pathlib import Path
import argparse
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]

def source_id():
    files = [ROOT / 'CMakeLists.txt']
    for name in ('src', 'scripts', 'tests', 'assets', 'packs'):
        files += [p for p in (ROOT / name).rglob('*') if p.is_file() and '__pycache__' not in p.parts]
    digest = hashlib.sha256()
    for path in sorted(files):
        digest.update(path.relative_to(ROOT).as_posix().encode() + b'\0')
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()

def file_hashes(folder):
    return {p.relative_to(folder).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(folder.rglob('*')) if p.is_file() and p.name != 'stage-build.json'}

def verify_embedded_identity(folder, identity):
    required = ('AutoKeyboard.exe','AutoKeyboardIME.dll','AutoKeyboardControl.exe',
                'AutoKeyboardPdfWorker.exe','AutoKeyboardTypingTest.exe','AutoKeyboardNotepadDiagnostic.exe')
    for name in required:
        path=folder/name
        if identity.encode('ascii') not in path.read_bytes():
            raise ValueError(f'{name} does not contain the current build identity; build before staging')

def validate_stage(folder, expected_arch=None):
    data = json.loads((folder / 'stage-build.json').read_text())
    if data['source'] != source_id():
        raise ValueError(f'Stale build in {folder}; rebuild and stage both architectures')
    if expected_arch and data['architecture'] != expected_arch:
        raise ValueError(f'Wrong architecture in {folder}')
    if data['files'] != file_hashes(folder):
        raise ValueError(f'Staged files changed in {folder}; rebuild and stage again')
    verify_embedded_identity(folder,data['source'])
    return data

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--header', type=Path)
    parser.add_argument('--stage', type=Path)
    parser.add_argument('--arch', choices=['x64', 'x86'])
    parser.add_argument('--version', default='0.2.0')
    parser.add_argument('--expected-source')
    parser.add_argument('--validate', action='store_true')
    args = parser.parse_args()
    identity = source_id()
    if args.header:
        text = f'#pragma once\n#define AK_VERSION "{args.version}"\n#define AK_SOURCE_ID "{identity}"\n'
        args.header.parent.mkdir(parents=True, exist_ok=True)
        if not args.header.exists() or args.header.read_text() != text:
            args.header.write_text(text)
    if args.stage:
        if args.validate:
            print(json.dumps(validate_stage(args.stage, args.arch)))
        else:
            if args.expected_source != identity:
                raise SystemExit('Source changed after configuration; rebuild before staging')
            verify_embedded_identity(args.stage,identity)
            data = dict(version=args.version, source=identity, architecture=args.arch, files=file_hashes(args.stage))
            (args.stage / 'stage-build.json').write_text(json.dumps(data, indent=2))
    if not args.header and not args.stage:
        print(identity)
