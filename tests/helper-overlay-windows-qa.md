# Native Windows overlay policy QA

This standalone harness builds the real `HelperOverlayPolicy.h` Win32 reader.
It does not start VNC, change capture settings, install a service or write INI.
`ReadNamed` is an isolated-test overload; `Read` keeps the fixed production name.

## Build and CI

Run from the repo root in a VS2022 x64 developer command prompt:

```bat
mkdir overlay-qa
cl /nologo /std:c++14 /EHsc /utf-8 /W4 /WX /MT tests\helper-overlay-windows-qa.cpp /Fe:overlay-qa\helper-overlay-windows-qa.exe /Fo:overlay-qa\native.obj
cl /nologo /std:c++14 /EHsc /utf-8 /W4 /WX /MT tests\helper-overlay-policy-test.cpp /Fe:overlay-qa\helper-overlay-policy-test.exe /Fo:overlay-qa\portable.obj
overlay-qa\helper-overlay-policy-test.exe
```

The `Helper overlay native QA` workflow (`helper-overlay-qa.yml`) uses
`windows-2022`, the installed `vcvars64.bat`, and `cl.exe`. No MFC, vcpkg or full
VNC build is needed. It runs the native self-test as SYSTEM via one short-lived,
uniquely named scheduled task, then removes that task in `finally`.
Artifact: `helper-overlay-native-qa`, containing two executables and JSONL report.
The existing full VNC push workflow is not changed by this work.

## Isolated native self-test

From an elevated PowerShell prompt:

```powershell
./tests/run-helper-overlay-system-tests.ps1 -Executable ./overlay-qa/helper-overlay-windows-qa.exe -Report ./overlay-qa/system-qa.jsonl
```

Use a fresh report filename. Existing reports are never overwritten by the
PowerShell wrapper. All test mappings use unique `Local\AppCenter.HelperOverlay.QA.*`
names and are released when the harness exits. Production mapping is untouched.

Alternatively, in an already SYSTEM context:

```bat
helper-overlay-windows-qa.exe --self-test --report C:\ProgramData\AppCenter\overlay-qa-new.jsonl
helper-overlay-windows-qa.exe --test-untrusted
```

The direct harness `--report` switch writes the explicitly named file; the
fresh-file protection belongs to the PowerShell wrapper, not this low-level switch.

Acceptance covers:

- Real owner/DACL representation accepted by `TrustedMapping`.
- Unpublished, expired, invalidated payloads rejected; absent object distinguished.
- Valid published bytes read with exact Turkish text/case/quotes.
- SYSTEM-owned mapping with AU write permission rejected.
- Administrators-owned mapping with otherwise expected ACL rejected.
- Restricted-token AU-only access check: read + READ_CONTROL succeeds;
  FILE_MAP_WRITE fails with ERROR_ACCESS_DENIED.

The restricted-token test is a real Windows access check, but it is **not** a
claim that a separate interactive standard-user process has been tested.

## Pilot Go writer ↔ native reader test

Root agent owns writer/test session orchestration. While the Go writer holds
the production mapping, run these read-only commands:

```bat
helper-overlay-windows-qa.exe --read Ready
helper-overlay-windows-qa.exe --read Ready --show-test-operator
```

JSON reports state/session/expiry/enabled. Operator text is redacted unless
`--show-test-operator` is present **and** the actual text starts with `QA-`.
Use synthetic names such as `QA-İpek ŞEN` for transport QA, never real operator
names with that prefix. Before writer publication or after release:

```bat
helper-overlay-windows-qa.exe --read Absent
```

For intentionally expired or invalid snapshots use `--read Invalid`.
Mismatch returns nonzero. Reader does not start or stop the agent/helper.

In a real **non-admin interactive user** process while the SYSTEM writer holds
the mapping:

```bat
helper-overlay-windows-qa.exe --check-access
```

This requires read/query success and explicit write denial. Running this command
as elevated admin or SYSTEM should fail the write-denial expectation; that is
not a product defect. It never modifies the mapping even if write access succeeds.

## Access-right rationale and remaining validation

Microsoft documents `READ_CONTROL` among file-mapping standard rights in
[File Mapping Security and Access Rights](https://learn.microsoft.com/en-us/windows/win32/memory/file-mapping-security-and-access-rights).
[OpenFileMapping](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-openfilemappinga)
checks requested rights against the object's descriptor. Therefore the reader
requests `FILE_MAP_READ | READ_CONTROL`: first for payload, second for owner/DACL
validation. Neither grants write permission.

[Restricted Tokens](https://learn.microsoft.com/en-us/windows/win32/secauthz/restricted-tokens)
explains the second access check against restricting SIDs. The harness restricts
that check to Authenticated Users instead of relying on the original SYSTEM/admin
token to demonstrate write denial.

These are test implementations, **not passed Windows results** until their CI
or pilot JSONL report shows PASS. Native Windows compilation and ACL behavior
cannot be inferred from the Linux portable parser test. Secure-desktop/login,
two-monitor windows and full VNC/noVNC acceptance remain separate integration gates.
