# Testing AutoKeyboard 0.2.0

Use the same build identity in both architectures and in the installed DLLs. A
passing dictionary preview does not certify Windows input delivery.

## Automated checks

Build x64 and x86 separately with `scripts/build.ps1`. Each build runs CTest before
staging. Then run `python scripts/run_tests.py --installed`. The runner executes
architecture suites sequentially and writes `build-package/test-results.json`.
Exit 0 means the requested checks passed; 1 means failure; 3 means a desktop check
was blocked. The report includes executable hashes and Windows/account metadata,
never ordinary document content. Do not change the Windows account to make a
blocked check appear successful.

`AutoKeyboardControl.exe --preflight` checks that the test account owns the
normal input desktop. `--status` separately reports registration, COM loading,
the loaded DLL path and its own build identity. A successful `--status` is not a
typing result. The x86 executable checks the 32-bit registration view.

The component suites cover decision rules, optional length filtering, real PDF
extraction, worker protocol validation, database undo, immutable bank snapshots,
protected input scopes, edit-lock/write failures, focus changes and controlled
queued input. The queued-input tests explicitly enable the experimental bridge.
Direct-callback and Windows-dispatched tests have different labels.

For a bounded synthetic soak run, use
`keyboard_tsf_tests.exe --soak-seconds 1800` (maximum 30 minutes). It verifies
mixed text in repeated batches, resets only its owned fixture, bounds timing
samples, and reports latency and the number of checked words. It does not
certify physical input or application compatibility.

## Physical typing on this PC

1. Save work. On this development PC, run `python scripts/backup_bank.py` from
   your normal account to create a verified online backup and SHA256 sidecar.
   This helper needs Python; the keyboard application does not. Then run the
   new installer directly and restart if updating. Keep the previous installer
   and the backup. Do not copy only
   the `.db` file while WAL writes are active; close AutoKeyboard and editors or
   use SQLite's online backup API.
2. Open AutoKeyboard normally, click **Activate keyboard**, and select the full
   **Automatic Arabic / English** profile using Win + Space.
3. Open `AutoKeyboardTypingTest.exe` in the installation folder. Click its editor
   and physically type `hgsghl`, Space, `hello`, Space. Expected: `السلام hello `.
   Click **Verify typed result**. It saves a result and metadata only in Local
   AppData. Repeat using `x86/AutoKeyboardTypingTest.exe`.
4. Test the same phrase in Notepad. For a trace, save and close existing Notepad
   windows, then open `AutoKeyboardNotepadDiagnostic.exe`. A trace contains
   bounded lifecycle/error metadata and no key values or document content.
   The launcher enables a ten-minute per-user consent marker so a fresh process
   can trace even if Windows activation does not inherit its environment.
   Newly activated AutoKeyboard hosts can contribute metadata during this window.
   `AutoKeyboardControl.exe --stop-trace` stops marker-based capture early.
5. Test Word if installed, and open `diagnostics/browser-fields.html` locally in the
   browser. Check input, textarea, contenteditable, dummy protected fields, and
   the submission counter. There is no network code in this fixture.
6. For each app, repeat three times and after the required restart: mixed text,
   Enter, Backspace, punctuation, Arabic lam-alef, modifiers/held keys, manual
   conversion, undo, language lock, both detection modes, optional short-word
   settings, and switching fields mid-word. Check the complete expected text,
   rather than only whether Arabic appeared.
7. Run a 30-minute typing/focus/import session. Record the app version, bitness,
   Windows build, installer hash, result, and any defect. Apps that are absent
   remain untested.

The physical test host is a real RichEdit control with a normal TSF-aware message
loop. Its result applies only to that host. Pasting the expected phrase does not
test keyboard conversion. Automated Unicode text entry does not replace physical
key tests.

## Locate an input-delivery failure

Compare the x64 and x86 preflights, loaded DLL identities, activation events,
foreground focus, `test-key`/`key-down` callbacks, and edit-session results. If
callbacks are absent, investigate registration/routing before dictionary rules.
If callbacks exist but edits fail, investigate document access and composition.

The thread-local message bridge is now enabled by default alongside native TSF
delivery. When native TSF processes a key, it replaces its virtual-key code with
VK_PROCESSKEY, so the bridge skips already-handled messages. For a comparison
without the bridge, set AUTOKEYBOARD_MESSAGE_BRIDGE=0 in the target process.
For a comparison with bridge only, the diagnostic launcher's `--bridge` flag
remains available. Record bridge and non-bridge results separately.

## Installer and recovery checks

Run `python scripts/verify_package.py` before distributing a candidate. It
extracts into a new owned folder, verifies hashes and architecture, and rejects
missing/changed files and a corrupted payload without installing anything.

The machine installer preserves known prior metadata, shortcut contents and
file backups during a failed update. Its pending-reboot guard prevents a new
install from reusing paths queued for deletion. Test the guard and failure paths
with isolated fixtures first. Live UAC, locked-host upgrade, alternate-admin
credentials, restart persistence and uninstall/reinstall still need a scheduled
run on this PC. Never delete pending Windows operations to force installation.

If rollback reports incomplete recovery, keep the uniquely named `.rollback-*`
folder, close editors, and run repair before restarting. No automatic test may
reboot Windows or close an unsaved user document.

## Release gate

Unsigned builds are development previews. A production release requires trusted
signing, successful signature verification, and recorded real-app acceptance.
Missing automation, signing, focus or applications must be reported as blocked
or untested, never as a pass.
