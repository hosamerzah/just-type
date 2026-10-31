# Repairing an incomplete 0.1.3 installation

The user uninstalled the old version, installed 0.1.3 before restarting, and
then restarted. Windows still registered 0.1.3, but the 64-bit input DLL, tray
executable, PDFium DLL and copied uninstaller were missing. The 32-bit DLL
matched the packaged build. COM returned `0x8007007E` for the missing 64-bit DLL.

This matches the old uninstaller's pending deletions: locked files were queued
at their live installation paths, and reinstalling before reboot reused those
paths. The repair installer keeps the 0.1.3 keyboard code and word-bank format.

The installer now recognizes a partial installation only when the machine's
AutoKeyboard uninstall record names the expected installation folder, even if
the tray executable or keyboard DLL is missing. Reparse-point and target-path
checks remain in place. It restores and byte-verifies every payload file before
registration and success are recorded. Personal vocabulary stays in Local AppData.

Before installing, it reads Windows' pending file-operation list and refuses to
replace live paths that will be deleted or renamed at restart. It asks the user
to restart first. It does not edit that system list. Old uniquely named rollback
copies do not conflict with new live files.

Run the repair installer directly; do not uninstall first. The read-only command
`AutoKeyboardSetup.exe --verify-installed` checks all packaged files in Program
Files. An optional directory argument supports checking an extracted package in
tests. Exit status zero means an exact match; nonzero means missing, altered or
unreadable files. This check does not register, install or enable a keyboard.

Reference: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw
