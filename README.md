# ReviveCE

ReviveCE brings modern, securely authenticated Internet services to Windows
Mobile 6.x devices. The first target is the HTC Touch Pro (Raphael), running
Windows Mobile 6.1 Professional on ARMV4I.

## Current milestone

The repository currently contains the **M0/M1 ReviveTLS proof application**:

- a native Win32/Windows CE user interface sized for a 480 x 640 device;
- a worker-thread network test so DNS timeouts do not freeze the UI;
- IPv4 DNS resolution through Winsock;
- a non-blocking TCP connection to `imap.gmail.com:993` with a 15-second
  timeout;
- local, credential-safe diagnostic logging;
- a fail-closed TLS placeholder.

The program deliberately reports `NOT BUILT` for TLS, certificate, and hostname
verification. No connection is ever presented as secure until wolfSSL is
integrated and all three checks pass on physical hardware.

The canonical build machine is GitHub Actions. The first workflow builds only a
CRT-free ARMV4I HelloWorld and rejects the result unless its PE headers identify
it as an ARM Windows CE 5.2 GUI program. This proves the legacy toolchain before
wolfSSL work begins.

See [docs/BUILDING.md](docs/BUILDING.md) for CI setup and device deployment,
[ci/README.md](ci/README.md) for the private toolchain archive contract, and
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

The repository check needs only PowerShell. ARM binaries are produced by the
GitHub workflow from a privately supplied, checksum-pinned licensed toolchain.
The emulator is useful for UI work, but the milestone only passes after testing
on the HTC Touch Pro itself.
