# Development-build verification — 2026-10-30

## Version 0.1.3 queued input and focus cleanup

The user's fresh 0.1.2 Notepad trace still showed activation/focus with no key
callbacks. That update did not resolve the reported failure. Version 0.1.3 adds
a thread-scoped queued-message bridge through TSF, removes dependence on a
shared open/close flag that Windows can restore independently, and fixes
composition cleanup across deferred edit locks and unrelated document events.
See `INPUT_DELIVERY_FIX.md` for boundaries and remaining application checks.

The suites contain 37 core checks, 29 TSF checks and 3 settings UI checks per
architecture. Both x64 and x86 pass all 69 checks (138 total). Native focus-sensitive suites
are run sequentially across architectures. Their owned custom window drains
pending activation/focus events before posting keys. Early concurrent runs and
the former STATIC-window fixture exposed intermittent test-focus failures;
those results were not used to certify an application.

Physical typing in Notepad, Word and browsers remains unverified for 0.1.3 until
the user installs it and restarts Windows. The desktop computer-use helper
still cannot launch its app-server executable. The installer is an unsigned
development preview.

## Version 0.1.2 keyboard-open initialization

The user's Notepad trace confirms successful activation with no key callbacks.
The installed 0.1.1 service left Windows' keyboard-open compartment at zero in
an owned RichEdit probe. Version 0.1.2 initializes that compartment to one and
honors explicit direct-input mode afterward, following Microsoft's SampleIME.
This addresses a missing initialization step; Notepad compatibility still needs
the user's physical-key confirmation after updating.

Both x64 and x86 pass 36 core checks, 20 TSF checks, and 3 settings UI checks:
59 per architecture, 118 total. The added checks verify the open state after
activation and native pass-through while closed. Package self-verification,
all 55 extracted-file hashes, both DLL architectures, and corruption rejection
pass. The personal bank's settings remain unchanged. The installer remains an
unsigned development preview.

## Version 0.1.1 activation repair

Both x64 and x86 builds pass 36 core checks, 18 TSF checks (including UI-less activation/conversion), and 3 native settings checks: 57 per architecture, 114 total. The installed 0.1.0 DLL also passed a separate single-word check through Windows' actual key dispatcher after correct test-window focus association. This proves that the installed DLL can load; it does not certify Notepad or identify UI-less activation as the confirmed cause of this user's Notepad failure.

The profile was added successfully through `InstallLayoutOrTip` and its entry was verified in Windows' enabled-input-method ordering. Version 0.1.1 also adds explicit activation/diagnostics controls and modern input-indicator categories. The new installer supports updating the existing installation while retaining the personal bank. Actual elevated update/rollback and visual Notepad behavior remain unverified until the user runs the updated build.

## Original 0.1.0 build

Built on Windows with LLVM-MinGW 20261020 and CMake. Both x64 and x86 builds pass:

| Suite | Checks per architecture | Result |
| --- | ---: | --- |
| Dictionary, detection, Unicode, PDFium, SQLite and COM | 36 | Pass |
| Windows TSF composition/range integration harness | 17 | Pass |
| Native settings UI and persistence | 3 | Pass |

This is 56 behavior checks per architecture (112 total), grouped into three CTest suites per build. PDFium's selected binary has V8 and XFA disabled. Neither application nor keyboard performs runtime network calls.

The TSF harness uses actual Windows context/range/edit-session objects and the real input DLL, with an adapter for profile key-sink registration. These checks do not certify actual keyboard profile activation in other applications.

The development installer is explicitly **unsigned**. No trusted code-signing certificate with a private key was present in the current user's certificate store. `scripts/package.ps1` includes a signing-and-verification path and requires either a certificate thumbprint or explicit `-UnsignedPreview`.

Installer self-check, extraction hashes, both embedded DLL architectures, and deliberate payload-corruption rejection are checked by `scripts/verify_package.py`. These checks do not install the keyboard or change Windows keyboard selection. A signed installation/uninstallation and the Notepad/Word/browser checks in `COMPATIBILITY.md` remain release acceptance work.
