# ReviveCE

ReviveCE brings modern, securely authenticated Internet services to Windows
Mobile 6.x devices. The first target is the HTC Touch Pro (Raphael), running
Windows Mobile 6.1 Professional on ARMV4I.

## Current milestone

The repository currently contains the **M0-M3 ReviveTLS proof application**:

- a native Win32/Windows CE user interface sized for a 480 x 640 device;
- a worker-thread network test so DNS timeouts do not freeze the UI;
- IPv4 DNS resolution through Winsock;
- a non-blocking TCP connection to `imap.gmail.com:993` with a 15-second
  timeout;
- local, credential-safe diagnostic logging;
- a statically linked, pinned wolfSSL 5.9.2 TLS 1.2 client;
- custom wolfSSL I/O over Winsock with `select()`-based deadlines compatible
  with Windows CE 5.2;
- SNI, certificate-chain verification, and hostname verification;
- Google's maintained 21-certificate service trust bundle, shipped beside the
  executable rather than using Windows Mobile's obsolete certificate store;
- a required encrypted IMAP greeting before the test reports success.

The M3 build reports TLS, certificate, and hostname status separately. Any
missing bundle, failed handshake, invalid chain, hostname mismatch, or missing
server greeting rejects the connection; plaintext fallback is never attempted.

The canonical build machine is GitHub Actions. The workflow uses a
digest-pinned, open-source CeGCC 9.3 container to build both the proven ARM
HelloWorld smoke test and the M3 `ReviveTLS.exe`. It rejects either result
unless its PE headers identify it as an ARM Windows CE 5.2 GUI program. No
repository secrets or proprietary compiler downloads are required.

See [docs/BUILDING.md](docs/BUILDING.md) for CI setup and device deployment,
[ci/README.md](ci/README.md) for the pinned CeGCC toolchain contract, and
[MVP.md](MVP.md) for the product specification.

## Layout

```text
ReviveTLS.sln             Visual Studio 2008 solution
.github/workflows/        Canonical ephemeral CI build
ci/                       ARMV4I bootstrap, build, and PE verification
revivece/
  app/                    WinCE application and UI
  common/                 diagnostics
  net/                    DNS, TCP, and TLS boundary
  tests/                  repository validation
docs/                     build and porting notes
```

## Quick validation

On a Windows development machine, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\revivece\tests\validate.ps1
```

The repository check needs only PowerShell. ARM binaries are produced inside a
digest-pinned public build container. The emulator is useful for UI work, but
the milestone only passes after testing on the HTC Touch Pro itself.
