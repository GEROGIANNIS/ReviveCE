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
artifacts are `WM6-ARMV4I-HelloWorld` and `ReviveTLS-M6-WM6-ARMV4I`.

## First device test: CI0-CI4

1. Push the workflow and CI scripts to GitHub.
2. Run the toolchain workflow, or let its path-filtered push trigger run it.
3. Download `WM6-ARMV4I-HelloWorld` from the completed run.
4. Check the included SHA-256.
5. Copy `HelloWorld.exe` to the HTC Touch Pro and launch it.

After HelloWorld launches, download `ReviveTLS-M6-WM6-ARMV4I`. Copy both
`ReviveTLS.exe` and `google-roots.pem` into the same directory on the phone,
then run the test over Wi-Fi. wolfSSL is linked statically, so no companion DLL
is required; the PEM file is ReviveCE's independently updateable trust store.

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

## M4 inbox, M5 reader, and M6 send test

The same executable includes the M4 inbox proof and M5 message reader. Enter a Gmail address
and an eligible Gmail **App Password**, then tap **REFRESH INBOX**. The password
is copied to a worker request, removed from the edit control immediately, and
kept only in the running application's memory, and zeroed when ReviveCE closes.
It is not saved between runs or written to disk.

The client accepts only the encrypted, certificate-verified IMAP path and then
uses `LOGIN`, `SELECT INBOX`, `UID SEARCH ALL`, and header-only `UID FETCH` to
list the latest 25 messages. Select an entry and tap **OPEN** to fetch and read
its plain-text body. The reader fetches a text section rather than the complete
message, so attachments are not downloaded; common first-part multipart text
and basic HTML-to-text conversion are supported. The reader loads 8 KiB first;
when **LOAD MORE** is enabled it re-fetches a larger prefix in 8 KiB increments
up to 32 KiB. Do not re-enter the App Password for **OPEN** while the app
remains running. A successful operation reports `MAIL ... OK`; a readable IMAP
failure description identifies messages that exceed the current bound or use
an unsupported format.

After an inbox refresh succeeds, tap **COMPOSE**. Enter a recipient, an ASCII
subject, and a short plain-text body, then tap **SEND**. The app makes a new
verified TLS connection to `smtp.gmail.com:465` and reuses the current
in-memory App Password. `SENT. Gmail accepted the message.` confirms SMTP
acceptance. Check the recipient mailbox separately. The compose form does not
support attachments, HTML, drafts, or non-ASCII subjects yet.

## Expected TLS test result

With Wi-Fi connected, tap **RUN TEST**. This first milestone should report:

```text
DNS .............. OK
TCP .............. OK
TLS 1.2 .......... OK
Certificate ...... OK
Hostname ......... OK
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
- **M2:** the pinned wolfSSL library initializes and cleans up without crashing
  on the physical phone.
- **M3:** TLS 1.2 negotiates with `imap.gmail.com`, its certificate chain and
  hostname verify against the bundled trust store, and an encrypted IMAP
  greeting is received on the physical phone.
- **M4:** with an eligible Gmail account and App Password, the physical phone
  displays up to 25 current INBOX headers over that verified TLS session.
- **M5:** selecting an inbox entry on the physical phone opens a scrollable
  plain-text body over a second verified IMAP session. Attachments are not
  downloaded.
- **M6:** after a successful refresh, the physical phone sends a plain-text
  message through Gmail SMTP over a separate verified TLS session and the
  recipient receives it.

An emulator run does not count as device acceptance. All five rows must report
`OK`; a successful TCP connection alone is not a TLS handshake.

## Repository validation

The validation script does not replace compilation. It catches missing project
files, malformed Visual Studio XML, accidental use of deprecated WinINet TLS,
and accidental plaintext fallback:

```powershell
powershell -ExecutionPolicy Bypass -File .\revivece\tests\validate.ps1
```
