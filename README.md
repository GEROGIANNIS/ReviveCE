# ReviveCE

ReviveCE brings modern, securely authenticated Internet services to Windows
Mobile 6.x devices. The first target is the HTC Touch Pro (Raphael), running
Windows Mobile 6.1 Professional on ARMV4I.

## Current milestone

The repository currently contains the **M0-M5 ReviveTLS/IMAP proof application**:

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
- alternate-chain validation for Google's appended cross-signed compatibility
  roots, without weakening peer verification;
- Google's maintained 21-certificate service trust bundle, shipped beside the
  executable rather than using Windows Mobile's obsolete certificate store;
- a required encrypted IMAP greeting before the TLS test reports success;
- a Gmail App Password form held only for the current refresh, never written
  to the log or settings;
- tagged IMAP `LOGIN`, `SELECT INBOX`, `UID SEARCH`, and header-only `UID
  FETCH` commands for the newest 25 messages, including sender, subject,
  date, and unread state.
- selectable inbox rows and a scrollable message reader that fetches a selected
  message over a new verified IMAP session;
- section-based IMAP retrieval: message headers are fetched first, then a text
  section rather than the complete RFC822 message, avoiding attachment
  downloads. Common single-part and first-part multipart plain text is shown;
  HTML is reduced to text when necessary.

The M5 build reports TLS, certificate, hostname, and IMAP status separately. Any
missing bundle, failed handshake, invalid chain, hostname mismatch, or missing
server greeting rejects the connection; plaintext fallback is never attempted.

The canonical build machine is GitHub Actions. The workflow uses a
digest-pinned, open-source CeGCC 9.3 container to build both the proven ARM
HelloWorld smoke test and the M5 `ReviveTLS.exe`. It rejects either result
unless its PE headers identify it as an ARM Windows CE 5.2 GUI program. No
repository secrets or proprietary compiler downloads are required.

See [docs/BUILDING.md](docs/BUILDING.md) for CI setup and device deployment,
[ci/README.md](ci/README.md) for the pinned CeGCC toolchain contract, and
[MVP.md](MVP.md) for the product specification.

## What the app shows on the phone

`ReviveCE Mail` opens with separate DNS, TCP, TLS 1.2, Certificate, Hostname,
and IMAP status rows, followed by Gmail address and App Password inputs.
`TEST TLS` proves the encrypted connection without logging in. `REFRESH INBOX`
authenticates with the App Password, clears that edit field, and fills the list
with up to 25 recent messages. Each row shows a `*` when unread, plus sender,
subject, and date.

Select a row and tap `OPEN` (or double-tap it) to fetch the chosen message.
The full-screen reader shows its sender, subject, date, and a scrollable
plain-text body. When only HTML is available, the reader labels its simplified
text conversion. The App Password is never written to logs or disk; it remains
only in memory until ReviveCE closes so selected messages can be opened.
The password field is cleared after refresh, but do not re-enter it for `OPEN`:
the active in-memory session is reused. If a message cannot be read, the IMAP
row now states the specific test outcome, such as `MESSAGE TEXT TOO LARGE` or
`MESSAGE FORMAT NOT SUPPORTED`, alongside its diagnostic code.

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
