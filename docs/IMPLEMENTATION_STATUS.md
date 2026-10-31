# Implementation status: 0.2.0 development candidate

This file describes source changes. Recorded test results are separate from
real application compatibility.

| Plan item | Implemented | Remaining evidence |
| --- | --- | --- |
| Reproducible build identity | Source fingerprint, DLL identity export, stage hashes, matching-architecture checks, locked dependency checksums | Validate final packaged candidate |
| Honest test environment | Account/desktop preflight, visible installed-profile host, blocked exit code, sequential evidence runner | Run in the intended user's normal desktop |
| Missing keystrokes | Bounded versioned lifecycle traces, independent native/bridge comparison, checked focus-sink registration | Root cause in Notepad and original x86 failure remains unproven |
| Composition robustness | Failure-path regression tests, stale-composition retirement, callback guards, oversized-word suffix fix, coalesced letter/space/Backspace handling | Real editor failure behavior and held-key matrix |
| Protected fields | Actual Windows password/URL/email/login scope tests and scope transition test | Browser and Word protected fields |
| Typing responsiveness | Background consistent SQLite snapshots, immutable vocabulary alias between words, query cancellation on shutdown, concurrent 100,000-word refresh test | Real-app latency measurements |
| PDF and vocabulary | Separate kill-on-close worker process, cancellation/timeout, bounded validated IPC, cancellable database transaction, stricter pack validation, English internal apostrophes | Live cancellation during unusually large/problem PDFs |
| Tray and settings | Explorer icon recreation, foreground/freshness validation, invalidation on deactivation, DPI scaling and vertical scrolling | Visual checks across monitor DPI and Explorer restart |
| Installer | Unelevated user configuration, partial-uninstall cleanup, file/record/shortcut rollback, preserved reboot guard, verified SQLite online-backup helper | UAC, alternate administrator, locked-host update and reboot tests |
| Release | Unsigned preview and signing pipeline with explicit limits | Trusted signing certificate and app certification |

Dependency checksums were locked from the existing pinned development inputs.
This detects changes on subsequent downloads; it is not an independent
provenance audit of those earlier inputs.

Normal TSF and the thread-local message bridge are both active by default. The bridge supplements native TSF delivery for applications that do not forward keystrokes; set AUTOKEYBOARD_MESSAGE_BRIDGE=0 to disable it. The
program continues to use local dictionaries and fixed rules, with no AI or OCR.
The minimum bank length remains optional; its default and the independent
automatic-switching default are four letters.

No candidate is described as a fix for Notepad until its physical typing result
is recorded. See TESTING.md for the procedure and the fallback when UI automation
cannot start.

The accessible older Notepad trace contained only a launcher header. The 0.2
launcher therefore uses expiring per-user consent instead of depending solely
on environment inheritance. New traces identify the DLL and rejected routing
guards. The original cause of missing callbacks remains unproven until a fresh
real-app trace is captured.
