# AutoKeyboard 0.2.0 validation

Date: 30 October 2026. Development preview; no real application is certified.

Source fingerprint:
`2a36b00ce99cbfc68ee0f7d0b98aaf6cebcf51a6ef2a9dec56d8af7933f56e52`

Fresh x64 and x86 build directories were configured with the local LLVM-MinGW
toolchain. Final component and controlled integration checks passed in both:

| Suite | x64 | x86 |
| --- | --- | --- |
| Core, dictionaries, PDF, snapshots and diagnostic consent | 44 cases passed | 44 cases passed |
| Controlled TSF composition and input | 41 cases passed | 41 cases passed |
| Installer-state rules | 10 cases passed | 10 cases passed |
| Settings UI | Passed | Passed |
| Build tools and online backup | 4 Python cases passed | Same 4 cases passed |

The Python checks are shared tooling tests; running them in both suites does
not make them architecture-specific coverage.

The controlled TSF timing sample measured p95 key processing at 0.0393 ms on x64
and 0.0545 ms on x86. While a 100,000-word snapshot loaded concurrently, in-memory
decision p95 was 0.0247 ms and 0.034 ms respectively. These measurements describe
the owned test fixtures, not Notepad, Word or browser latency.

An additional owned-process fixture loaded a DLL, renamed it to a retired path,
and restored it successfully. This checks Windows loader/file behavior, not a
complete live application upgrade.

Additional bounded soak runs passed in both architectures. The x64 fixture
verified 213,800 mixed-language words in 60 seconds; x86 verified 166,200 words
in 60 seconds. Each batch checked the complete expected text. Warm key p95 was
0.0394 ms and 0.0504 ms respectively. The requested 30-minute physical
typing/focus/import session remains unperformed.

The final unsigned installer passed payload self-verification, extraction of all
68 files with matching SHA256 hashes, x64/x86 PE checks, execution-manifest
checks, rejection of missing/modified installed files and rejection of a
corrupted payload. Installer SHA256:
`979d003a0b2a6a7b8a9bfc2c2130a6a40a022f20810e463530585decb2f2946c`

Before the user was asked to install, the online-backup helper created and
verified a bank backup in the normal user's Local AppData backup folder. Its
SHA256 is
`e15d767f9dd8480a3770a85b237537dfe2262287d532ca4266e2c1f908224a7c`.
No vocabulary or document content was printed by that backup operation.

## Blocks encountered and handled

- Windows refused to start the 32-bit installer-state test without an explicit
  execution manifest. Adding an `asInvoker` manifest corrected the launch; all
  ten cases then passed. No Windows security policy was changed.
- The default tool account differs from the desktop user. A permitted read-only
  probe outside that account confirmed the normal desktop owner and passed the
  preflight in both architectures.
- The installed-profile probe still did not obtain TSF thread keyboard focus in
  its visible owned window. Both architectures correctly reported **blocked**.
  This supersedes interpreting an unfocused probe as a compatibility pass/fail.
- Windows UI automation could not launch its app-server executable (error 2).
  Physical typing in real applications remains unverified.
- The accessible prior Notepad trace contained only its launcher header. The
  new expiring diagnostic consent mechanism was tested without inherited trace
  environment variables, including expiry, stopping and DLL build identity.

The machine installation was not changed during these development tests.
Installation, real UAC/alternate-administrator handling, restart persistence,
real editor typing and signed release validation remain separate acceptance
steps. Use TESTING.md and record every blocked or untested case explicitly.

Local detailed evidence lives in `build-package/test-results.json`, the earlier
pre-fix report, and the additional soak/package check results. Those local
diagnostics include account/process metadata and are not embedded as public
typing-history data in the installer.
