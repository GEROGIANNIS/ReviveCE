# Building and testing ReviveCE

## Canonical GitHub build

No legacy development tools need to be installed on the user's computer. The
canonical builder is `.github/workflows/toolchain-test.yml`, pinned to the
`ubuntu-22.04` GitHub-hosted image and a specific CeGCC 9.3 container digest.

The container is public and built from open source specifically for Windows CE
ARM and x86 development. No repository secrets or private archives are needed.
See `ci/README.md` for the pinned toolchain and source repositories.

Run **WM6 ARMV4I Toolchain Test** manually from the repository's Actions tab.
It can also run after changes to the workflow or `ci/` scripts. Its downloadable
artifacts are `WM6-ARMV4I-HelloWorld` and `ReviveTLS-M1-WM6-ARMV4I`.

## First device test: CI0-CI4

1. Push the workflow and CI scripts to GitHub.
2. Run the toolchain workflow, or let its path-filtered push trigger run it.
3. Download `WM6-ARMV4I-HelloWorld` from the completed run.
4. Check the included SHA-256.
5. Copy `HelloWorld.exe` to the HTC Touch Pro and launch it.

After HelloWorld launches, download `ReviveTLS-M1-WM6-ARMV4I`, copy
`ReviveTLS.exe` to the phone, and run the M1 network test over Wi-Fi.

The workflow rejects desktop x86/x64 output by directly parsing the executable
headers. It requires ARM machine `0x01c0`, PE32, Windows CE GUI subsystem `9`,
and subsystem version `5.2`.

## Optional local ReviveTLS build

Developers who already maintain a compatible legacy VM can still build the
M0/M1 network proof locally with:

- Visual Studio 2008 SP1 with C++ Smart Device Programmability
- Windows Mobile 6 Professional SDK Refresh
- Windows Mobile Device Center or ActiveSync for deployment

1. Open `ReviveTLS.sln` in Visual Studio 2008.
2. Select `Debug` and `Windows Mobile 6 Professional SDK (ARMV4I)`.
3. Build the solution.
4. Choose the connected Windows Mobile device as the deployment target.
5. Deploy and run `ReviveTLS.exe`.

If the localized SDK installed on the development machine uses a different
platform display name, use Visual Studio's Configuration Manager to retarget
the project to its installed ARMV4I Windows Mobile 6 Professional platform.

## Expected result

With Wi-Fi connected, tap **RUN TEST**. This first milestone should report:

```text
DNS .............. OK
TCP .............. OK
TLS 1.2 .......... NOT BUILT
Certificate ...... NOT BUILT
Hostname ......... NOT BUILT
```

The first run can trigger Windows Mobile's connection-selection UI. A failed
test displays the native Winsock error beside the failing stage and can be run
again safely.

Diagnostics are appended to:

```text
\Application Data\ReviveCE\revive.log
```

The log records stages and numeric errors, never credentials or application
payloads.

## Milestone acceptance

- **CI0-CI3:** GitHub Actions uploads a PE-verified ARMV4I HelloWorld artifact.
- **CI4 / M0:** the HelloWorld ARM executable launches on the physical phone
  (confirmed on the initial HTC Touch Pro target).
- **M1:** DNS resolves `imap.gmail.com` and TCP connects to port 993 over Wi-Fi.

An emulator run does not count as device acceptance. TLS, certificate, and
hostname rows must remain `NOT BUILT` until the wolfSSL-backed implementation
passes its own physical-device checks.

## Repository validation

The validation script does not replace compilation. It catches missing project
files, malformed Visual Studio XML, accidental use of deprecated WinINet TLS,
and accidental plaintext fallback:

```powershell
powershell -ExecutionPolicy Bypass -File .\revivece\tests\validate.ps1
```
