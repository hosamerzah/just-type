# Version 0.1.3: queued input and focus cleanup

The user's fresh 0.1.2 Notepad trace showed activation, a successful Windows
keyboard-open update, context creation and focus notifications, with no key
callbacks. Initializing the open compartment did not resolve that report.

The input DLL now installs a `WH_GETMESSAGE` filter only on its own activating
thread. Other hooks run first. Remaining raw key messages are offered through
Windows' `ITfKeystrokeMgr::TestKeyDown/KeyDown` and corresponding key-up APIs.
Messages consumed by TSF become `WM_NULL`; native actions such as Enter remain
available to the host when the service returns them unconsumed.

The filter requires this service to be the foreground keyboard sink, the
message to target the focused window, and that window to belong to the focused
TSF context's view. Menus and window move/size loops are excluded. Existing
password, disabled-context, read-only and executable-exclusion checks apply.
The filter is removed on deactivation. It installs no global keyboard hook,
uses no `SendInput`, and does not inject the DLL into other applications.
When an application refuses the thread filter, normal TSF delivery remains
available; opt-in tracing records the filter installation error.

The bilingual profile also no longer stops processing solely because Windows
restores its shared East Asian IME open/close compartment to zero. Selecting
another keyboard profile deactivates it. Locking its detected language remains
available through the tray and shortcut.

Focus cleanup now requests an edit session that can run asynchronously, keeping
an owned reference to the old composition. It ends that composition without
rewriting text or using the new field's state. Redundant focus notifications and
unrelated document removal no longer interrupt an active word.

Private/test bank instances use separate tray-status mappings, so integration
tests cannot replace the running user's language/mode indicator with test state.

Integration checks post keyboard messages to a window owned by the test, and
verify conversion through the filter, exact dispatch counts, `PM_NOREMOVE`,
native Enter once, normal host forwarding, disabled/read-only fields, another
foreground keyboard, deferred edit locks, and unrelated document notifications.
They do not certify physical typing in Notepad, Word or browser controls.

After updating, restart Windows to unload copies of the previous DLL in
Explorer, Start/search and existing applications. Then select the profile and
test physical keys `hgsghl` followed by Space in Notepad.

References:

- https://learn.microsoft.com/en-us/windows/win32/winmsg/getmsgproc
- https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfkeystrokemgr-testkeydown
- https://learn.microsoft.com/en-us/windows/win32/tsf/tf-es--constants
