# Notepad diagnostic capture

The user reports that 0.1.1 converts in File Explorer but does not convert in
Notepad even while AutoKeyboard remains selected. A read-only module check on
2026-10-30 confirmed that both Explorer and Windows Notepad 11.2607.14.0 x64
loaded `C:\Program Files\AutoKeyboard\AutoKeyboardIME.dll`. This rules out a
missing DLL in that observed Notepad process; it does not establish the cause.

The user's diagnostic retry recorded `activate` and `ready` for Notepad PID
31080, with no `test-key` or `key-down` events. The trace therefore points to
key delivery before the conversion engine, rather than a failed dictionary
lookup or edit-session error. A separate probe using the installed 0.1.1
service in an owned RichEdit host found the Windows keyboard-open compartment
set to zero after activation. Directly dispatching synthetic TSF callbacks in
that hidden test host did not establish Notepad compatibility.

Version 0.1.2 opens `GUID_COMPARTMENT_KEYBOARD_OPENCLOSE` during activation,
following Microsoft's `InitializeSampleIMECompartment` example, and honors
explicit closed/direct-input state afterward. It also adds thread IDs and
focus/composition lifecycle events to opt-in traces. This is a candidate repair
pending a physical-key test in the user's Notepad window.

Reference: https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/IME/cpp/SampleIME/CompositionProcessorEngine.cpp

Build `AutoKeyboardNotepadDiagnostic`, or use
`AutoKeyboardControl --trace-notepad`. Save work and close all existing Notepad
windows first. The launcher refuses to proceed while Notepad is running,
because an existing process would not inherit its diagnostic environment.

The launcher sets `AUTOKEYBOARD_TRACE` only for its process and the Notepad
process it starts. It neither installs a keyboard nor changes persistent Windows
environment variables. Select AutoKeyboard and type `hgsghl` followed by Space.
The log is `%LOCALAPPDATA%\AutoKeyboard\notepad-diagnostic.log`.

The installed input DLL records process IDs, event names, HRESULT error codes
and numeric state flags. It does not record virtual keys, characters, words or
document text. Each capture replaces the previous diagnostic log. Close the
diagnostic Notepad process to end capture.

Interpret `activate`, `ready`, `test-key`, `key-down` and
`edit-session-failed` events before choosing a repair. A header-only log does
not prove that activation never occurred: first verify that the actual Notepad
process inherited the tracing environment.
