# Version 0.1.1: keyboard activation correction

The original tray application could run and change detection modes while Windows' keyboard integration was incomplete. Registration, enabling a profile, and selecting it are separate operations.

Changes:

- Enable/remove the profile with Windows' supported `InstallLayoutOrTip` API, preserving other input methods.
- Accept `TF_TMAE_UIELEMENTENABLEDONLY`, which is not secure mode. Reject secure mode as before.
- Register modern input-indicator, immersive and UI-less capabilities.
- Add **Activate keyboard** and **Diagnostics** to settings and the tray menu.
- Publish service status on activation rather than waiting for the first processed word.
- Update recognized installations with backups and rollback; personal data stays in Local AppData.
- Isolate test instances so regressions cannot send a lock command to the user's settings application.

Run the 0.1.1 installer, reopen Notepad, open AutoKeyboard from Start and click **Activate keyboard**. Select the full **Automatic Arabic / English** entry with Win + Space if necessary. Test physical keys `hgsghl` followed by Space. EN/AR on the Windows indicator alone is not an activation test.

Validation includes UI-less activation/conversion, activation/diagnostic controls, and an opt-in check using Windows' real key dispatcher with the currently installed DLL. Visual Notepad/Word/browser compatibility needs confirmation after updating. The desktop computer-use helper was unavailable during this repair: its app-server executable could not start. No screen behavior was inferred from that failure.
