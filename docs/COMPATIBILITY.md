# Signed-build acceptance checklist

No application below has been manually certified by the automated test suites. Record Windows build, app version, application bitness, signed installer hash, result and any limitations for every run.

| Application/editor | Status |
| --- | --- |
| Windows Notepad | Not yet verified |
| Microsoft Word document body | Not yet verified |
| Edge/Chrome plain input and textarea | Not yet verified |
| Edge/Chrome contenteditable editor | Not yet verified |
| 32-bit Word or other desktop TSF editor | Not yet verified |

1. Install the signed dual-architecture build. Verify both DLLs and setup signatures. Select Automatic Arabic / English with Win + Space; verify the profile loads and the tray reports changes.
2. Type `hgsghl ` and then `hello `. Expected: `السلام hello `, with exactly one space at each boundary. Repeat with Enter in a document and in a Send/Submit field; the action must happen once, after word commitment.
3. Type physical keys `;jhf ` for `كتاب `, and words containing lam-alef. Check Shift, Caps Lock, Arabic punctuation, repeated keys, rapid typing, and Backspace.
4. Test Cautious/Automatic with a deliberately ambiguous imported pair. Verify frequencies and a saved preference affect Automatic, while Cautious preserves ambiguous words.
5. Test short words with each bank/switch minimum combination. Disable the bank filter, import short words, and keep the automatic minimum at four; then lower the switch minimum. Manual conversion must work in both cases.
6. Convert the current word and the completed word; undo while immediately after the word. Move the caret, click another field, select text, paste, or use an editing shortcut; old cached text must never replace unrelated text.
7. Verify passwords, login/URL/email input scopes, read-only fields, excluded applications, and secure desktop are passed through. Do not use real credentials during testing.
8. Import a text PDF, a mixed text/image PDF and a corrupted PDF. Confirm page reports, normalization, counts, cancellation and per-import rollback, while typing in another application.
9. Save settings and change modes from the tray. Verify settings refresh between words. Restart the app and Windows; the profile and local bank must remain available.
10. Uninstall while a host has loaded the DLL, restart if required, and confirm the input profile and startup entry are gone while the personal bank remains.
