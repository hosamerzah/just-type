"""Exercise installer integrity/extraction without installing or registering it."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import struct
import subprocess
import ctypes
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('installer', nargs='?', type=Path, default=ROOT / 'dist/AutoKeyboard-0.2.0-unsigned-preview-setup.exe')
args = parser.parse_args()
installer = args.installer.resolve()
work = ROOT / 'build-package' / f'verify-{os.getpid()}'
work.mkdir(parents=True)


def run(path, *arguments):
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    return subprocess.run([str(path), *map(str, arguments)], timeout=30, startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW).returncode

def verify_manifest(path):
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.LoadLibraryExW.argtypes=[ctypes.c_wchar_p,ctypes.c_void_p,ctypes.c_uint32];kernel.LoadLibraryExW.restype=ctypes.c_void_p
    kernel.FindResourceW.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p];kernel.FindResourceW.restype=ctypes.c_void_p
    kernel.LoadResource.argtypes=[ctypes.c_void_p,ctypes.c_void_p];kernel.LoadResource.restype=ctypes.c_void_p
    kernel.LockResource.argtypes=[ctypes.c_void_p];kernel.LockResource.restype=ctypes.c_void_p
    kernel.SizeofResource.argtypes=[ctypes.c_void_p,ctypes.c_void_p];kernel.SizeofResource.restype=ctypes.c_uint32
    kernel.FreeLibrary.argtypes=[ctypes.c_void_p]
    module=kernel.LoadLibraryExW(str(path),None,0x22) # data/resource access only; never execute code
    assert module, f'Cannot read resources: {path}'
    try:
        resource=kernel.FindResourceW(module,1,24);assert resource, f'Missing application manifest: {path}'
        pointer=kernel.LockResource(kernel.LoadResource(module,resource))
        length=kernel.SizeofResource(module,resource);assert pointer and length, f'Cannot read application manifest: {path}'
        xml=ET.fromstring(ctypes.string_at(pointer,length))
        level=xml.find('.//{urn:schemas-microsoft-com:asm.v3}requestedExecutionLevel')
        assert level is not None and level.attrib.get('level')=='asInvoker', f'Unexpected elevation manifest: {path}'
    finally:kernel.FreeLibrary(module)


assert run(installer, '--verify-payload') == 0, 'Payload self-check failed'
print('PASS installer validates embedded payload')
extracted = work / 'extracted'
assert run(installer, '--extract', extracted) == 0, 'Extraction failed'
manifest = json.loads((ROOT / 'build-package/manifest.json').read_text(encoding='utf-8'))
for entry in manifest:
    path = extracted / entry['path']
    assert path.stat().st_size == entry['bytes'], entry['path']
    assert hashlib.sha256(path.read_bytes()).hexdigest() == entry['sha256'], entry['path']
print(f'PASS all {len(manifest)} extracted files match their SHA256 hashes')
for name in ('AutoKeyboard.exe','AutoKeyboardControl.exe','AutoKeyboardPdfWorker.exe','AutoKeyboardTypingTest.exe','AutoKeyboardNotepadDiagnostic.exe','x86/AutoKeyboardTypingTest.exe','x86/AutoKeyboardControl.exe'):
    verify_manifest(extracted/name)
verify_manifest(installer)
print('PASS installer and application manifests preserve the original user until explicit elevation')
assert run(installer, '--verify-installed', extracted) == 0, 'Complete installation verification failed'
missing = extracted / 'AutoKeyboardIME.dll'
saved = missing.with_suffix('.saved')
missing.rename(saved)
assert run(installer, '--verify-installed', extracted) != 0, 'Missing installed DLL was accepted'
saved.rename(missing)
modified = extracted / 'README.md'
previous = modified.read_bytes()
modified.write_bytes(previous + b'changed')
assert run(installer, '--verify-installed', extracted) != 0, 'Changed installed file was accepted'
modified.write_bytes(previous)
print('PASS installed-file verification accepts complete files and rejects missing/changed files')
for filename, expected in [('AutoKeyboardIME.dll', 0x8664), ('x86/AutoKeyboardIME.dll', 0x14c)]:
    blob = (extracted / filename).read_bytes()
    pe_offset = struct.unpack_from('<I', blob, 0x3c)[0]
    assert blob[pe_offset:pe_offset + 4] == b'PE\0\0'
    assert struct.unpack_from('<H', blob, pe_offset + 4)[0] == expected, filename
print('PASS installer contains both x64 and x86 input DLLs')
original = installer.read_bytes()
payload = (ROOT / 'build-package/payload.bin').read_bytes()
offset = original.find(payload[:128])
assert offset >= 0 and original[offset:offset + len(payload)] == payload, 'Cannot locate embedded payload'
broken = bytearray(original)
broken[offset + len(payload) - 1] ^= 1
corrupt = work / 'corrupt-installer.exe'
corrupt.write_bytes(broken)
assert run(corrupt, '--verify-payload') != 0, 'Corruption was accepted'
print('PASS installer rejects corrupted file contents')
print('No keyboard registration, installation, or user data was changed.')
