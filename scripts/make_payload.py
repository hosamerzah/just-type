"""Build a checksummed installer payload from staged, already-signed binaries."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
from build_identity import validate_stage

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--x64', type=Path, default=ROOT / 'dist/x64')
parser.add_argument('--x86', type=Path, default=ROOT / 'dist/x86')
parser.add_argument('--output', type=Path, default=ROOT / 'build-package')
parser.add_argument('--check-only', action='store_true')
args = parser.parse_args()
build64=validate_stage(args.x64,'x64')
build32=validate_stage(args.x86,'x86')
if build64['source']!=build32['source'] or build64['version']!=build32['version']:
    raise SystemExit('Architecture stages do not belong to the same build')
if args.check_only:
    print('Both staged architectures match current source and file hashes')
    raise SystemExit(0)
args.output.mkdir(parents=True, exist_ok=True)
files = [(path.relative_to(args.x64).as_posix(), path) for path in sorted(args.x64.rglob('*')) if path.is_file()]
if not files or not (args.x64 / 'AutoKeyboardIME.dll').exists():
    raise SystemExit('Stage an x64 build before packaging')
if not (args.x86 / 'AutoKeyboardIME.dll').exists():
    raise SystemExit('Stage an x86 build before packaging (required for 32-bit apps)')
files.append(('x86/AutoKeyboardIME.dll', args.x86 / 'AutoKeyboardIME.dll'))
files.append(('x86/AutoKeyboardTypingTest.exe', args.x86 / 'AutoKeyboardTypingTest.exe'))
files.append(('x86/AutoKeyboardControl.exe', args.x86 / 'AutoKeyboardControl.exe'))
files.append(('x86/stage-build.json',args.x86 / 'stage-build.json'))
manifest = []
with (args.output / 'payload.bin').open('wb') as out:
    out.write(b'AKP1' + struct.pack('<I', len(files)))
    for name, path in files:
        data = path.read_bytes()
        digest = hashlib.sha256(data).digest()
        encoded = name.encode('utf-8')
        out.write(struct.pack('<I', len(encoded)) + encoded + struct.pack('<Q', len(data)) + digest + data)
        manifest.append({'path': name, 'bytes': len(data), 'sha256': digest.hex()})
(args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
resource = f'101 RCDATA "{(args.output / "payload.bin").resolve().as_posix()}"\n'
resource += f'1 ICON "{(ROOT / "assets/keyboard.ico").as_posix()}"\n'
resource += f'1 24 "{(ROOT / "assets/application.manifest").as_posix()}"\n'
(args.output / 'setup.rc').write_text(resource, encoding='utf-8')
print(f'Payload: {len(files)} files; {sum(x["bytes"] for x in manifest):,} bytes')
