# AutoKeyboard troubleshooting report

**Update, 30 October 2026:** A 0.2.0 development candidate now includes further
source repairs, diagnostic tools, protected-field tests, background bank loading,
an isolated PDF worker, and packaging safeguards. Its x64 and x86 controlled
suites pass. The actual installed-profile probes remain blocked by missing TSF
thread focus, and real-app typing is still unverified. See
[the new validation report](docs/VALIDATION_0_2_0.md) and
[implementation status](docs/IMPLEMENTATION_STATUS.md). The 0.1.3 account below
is the earlier troubleshooting history, not a claim that 0.2.0 fixes Notepad.

**Date:** 30 October 2026  
**Version:** 0.1.3, with the repaired installer

**Current status:** The installation is now complete and verified. The installed 64-bit keyboard passed a conversion probe. The 32-bit conversion probe failed. Reliable physical typing in Notepad, Word, and browsers has not yet been confirmed after the repair.

**The problem**

The goal is to type Arabic and English without switching keyboards manually. The program uses local dictionaries and fixed rules, without AI. The minimum word length for building the bank is optional.

During use, the tray application and keyboard icon appeared, but typing `hgsghl` followed by Space did not produce `السلام` in Notepad. The user reported that conversion worked in File Explorer and Start search, then sometimes stopped. Selecting the keyboard did not consistently resolve the problem.

**Solutions tried**

| Attempt | What changed | Result |
| --- | --- | --- |
| Initial version, 0.1.0 | Built the Windows TSF keyboard, Arabic/English dictionary engine, tray settings, and PDF importer. | Automated component tests passed, but the user reported no conversion in Notepad. |
| Activation update, 0.1.1 | Improved Windows profile registration and enabling, added modern input-indicator and UI-less capabilities, and added activation and diagnostic controls. | The icon appeared. The user still reported failures in ordinary apps. |
| Open-state update, 0.1.2 | Set Windows' IME open state during activation. | Fresh Notepad logs showed successful activation and focus events, but no key callbacks. This update did not resolve the reported Notepad failure. |
| Input and focus update, 0.1.3 | Added a fallback that forwards queued keys through TSF inside the keyboard's own thread. Fixed delayed composition cleanup and unrelated focus notifications. Removed dependence on a Windows open/close flag that could disable processing. Isolated test tray indicators from the user's indicator. | The final automated input tests passed. Physical typing in the affected apps still needs confirmation. |
| Installer repair | Added recovery for missing program files, file-content verification, and a check that blocks installation when conflicting file deletions or renames are pending at restart. | After the user ran the repair, all 57 packaged files matched and Windows could load both keyboard DLLs. |

**The installation problem found**

After a restart, Windows listed version 0.1.3, but the tray application, 64-bit keyboard DLL, PDF library, and copied uninstaller were missing. The keyboard loader returned `0x8007007E`.

The user confirmed uninstalling the old version and reinstalling before restarting. The old uninstaller queued locked files for deletion at restart. Reinstalling reused those paths, allowing the pending deletions to remove replacement files. This explains the missing-file pattern. The repaired installer now blocks that sequence and can restore a partial installation. The personal word bank remained present in Local AppData.

**Checks completed**

- The final main suites passed **138 checks across 64-bit and 32-bit builds**: 37 core checks, 29 TSF checks, and 3 settings checks per architecture.
- These covered dictionary decisions, both language directions, short-word settings, Arabic normalization and ligatures, punctuation, Backspace, manual conversion, undo, focus changes, PDF extraction/import, and database rollback.
- The queued-input tests covered duplicate prevention, Enter once, disabled/read-only fields, and another selected keyboard profile.
- **10 installer-state checks passed.** Package checks also verified extraction hashes, both DLL architectures, rejection of corrupted payloads, and detection of missing or altered installed files.
- After repair, **all 57 installed payload files matched**. Both keyboard DLLs loaded successfully through Windows COM registration.
- A separate probe using the installed keyboard and Windows' key dispatcher **passed in 64-bit mode**. The equivalent **32-bit probe failed because a letter was not consumed**.

**What could not be verified or fixed completely**

- The exact cause of Notepad's missing key callbacks was not conclusively established. The logs show the symptom; the fixes have not yet been confirmed by physical typing after the latest repair.
- The 32-bit conversion failure remains unresolved. Restoring the test window's document/focus association did not make that probe pass. This does not establish whether the fault is in the probe, Windows routing, or the keyboard implementation.
- Automated Windows desktop inspection could not run because its app-server executable could not start, reporting “file not found” (error 2). Therefore, I could not independently operate and visually confirm Notepad, Word, or browser typing.
- Some early automated runs had intermittent focus-related failures. The test window and activation setup were adjusted, and the architecture suites were run sequentially. The final main suites passed, but those results do not certify real application compatibility.
- A signed release installer was not produced because no trusted code-signing certificate was available. The delivered installers remain unsigned previews.
- Repeated real-app tests for focus switching, shortcuts, protected fields, and lost or duplicated characters remain incomplete.

**What remains to be done**

For the new candidate, use the verified 0.2.0 installer directly, restart, and
record the physical typing result. A verified online backup of the personal
bank has already been made. Both architecture suites and the additional
60-second mixed-word soak runs passed; the final package's 68 files and both
architectures passed integrity checks. This still does not certify Notepad,
Word or browsers. The steps below describe the earlier 0.1.3 repair handoff.

Restart Windows while keeping the repaired installation. Test `hgsghl` followed by Space in Notepad with **Automatic Arabic / English** selected. Then check mixed Arabic/English typing and focus changes in Notepad, Word, browser fields, Explorer, and Start search. Investigate the 32-bit probe separately before claiming support for 32-bit applications.
