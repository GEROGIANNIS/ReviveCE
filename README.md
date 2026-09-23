# ReviveCE

ReviveCE brings modern, securely authenticated Internet services to Windows
Mobile 6.x devices. The first target is the HTC Touch Pro (Raphael), running
Windows Mobile 6.1 Professional on ARMV4I.

## Current milestone

The repository currently contains the **M0-M8 ReviveTLS mail, HTTPS, and feed proof application**:

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
- a reviewed Google and Let's Encrypt service trust bundle, shipped beside the
  executable rather than using Windows Mobile's obsolete certificate store;
- a required encrypted IMAP greeting before the TLS test reports success;
- a Gmail App Password held only in the active application session, never
  written to the log or settings;
- an optional app-local encrypted account file so a remembered Gmail
  account can start without showing the login fields;
- tagged IMAP `LOGIN`, `SELECT INBOX`, `UID SEARCH`, and header-only `UID
  FETCH` commands for the newest 25 messages, including sender, subject,
  date, and unread state.
- selectable inbox rows and a scrollable message reader that fetches a selected
  message over a new verified IMAP session;
- section-based IMAP retrieval: message headers are fetched first, then a text
  section rather than the complete RFC822 message, avoiding attachment
  downloads. Common single-part and first-part multipart plain text is shown;
  HTML is reduced to text when necessary.
- a modeless compose screen that sends a bounded plain-text message through
  Gmail SMTP over a separate, certificate-verified TLS 1.2 connection.
- a bounded HTTPS `GET` screen supporting verified `https://` URLs, ordinary
  and chunked HTTP/1.1 bodies, and an identity-encoded 32 KiB text prefix.
- an RSS 2.0 and Atom 1.0 feed screen that retrieves up to 25 item titles,
  publication dates, and links through that same verified HTTPS path.

## Device-test progress

The M3, M4, and initial M5 flows have been exercised successfully on the
physical HTC Touch Pro over Wi-Fi:

- M3: DNS, TCP, TLS 1.2, certificate-chain validation, hostname validation,
  and the encrypted Gmail IMAP greeting all complete successfully.
- M4: Gmail App Password authentication, INBOX selection, and retrieval of
  the newest 25 header rows work directly from the phone.
- M5: selected small messages open in the scrollable reader through a second,
  verified IMAP session.

M5.1 reads the first 8 KiB of a message text section initially. When more text
is available, the reader enables **LOAD MORE** and safely re-fetches a larger
prefix in 8 KiB increments, up to 32 KiB. This keeps each IMAP transfer bounded
while avoiding MIME decoding errors at arbitrary chunk boundaries. Attachments
are never downloaded; text beyond 32 KiB remains a later pagination task.

M6 is confirmed on the physical phone. It reuses the active
in-memory Gmail App Password, performs SMTP `EHLO` and `AUTH LOGIN`, then sends
a plain-text UTF-8 message with bounded recipient, subject, and body fields.
It has no attachments, HTML composition, drafts, or sent-mail view yet.

M7 is confirmed on the physical phone. The **WEB GET**
screen starts with `https://www.google.com/robots.txt`; it verifies the URL's
hostname and certificate before showing a bounded response. Redirects,
compressed responses, downloads, and cookies are intentionally deferred.

M8 adds **FEEDS**, with selectable Hacker News RSS endpoints plus a manual HTTPS
URL field. It downloads an RSS 2.0 or Atom 1.0 document and lists up to 25
titles with dates. Feeds are not saved, article pages and summaries cannot be
opened yet, and the 32 KiB HTTP response bound still applies.

The build reports TLS, certificate, hostname, and SERVICE status separately. Any
missing bundle, failed handshake, invalid chain, hostname mismatch, or missing
server greeting rejects the connection; plaintext fallback is never attempted.

The canonical build machine is GitHub Actions. The workflow uses a
digest-pinned, open-source CeGCC 9.3 container to build both the proven ARM
HelloWorld smoke test and the M8 `ReviveTLS.exe`. It rejects either result
unless its PE headers identify it as an ARM Windows CE 5.2 GUI program. No
repository secrets or proprietary compiler downloads are required.

See [docs/BUILDING.md](docs/BUILDING.md) for CI setup and device deployment,
[ci/README.md](ci/README.md) for the pinned CeGCC toolchain contract, and
[MVP.md](MVP.md) for the product specification.

## What the app shows on the phone

`ReviveCE Mail` opens with separate DNS, TCP, TLS 1.2, Certificate, Hostname,
and SERVICE status rows. On a new device it shows Gmail address and App
Password inputs plus **REMEMBER ACCOUNT**. When an account is remembered, the
login fields are hidden and the home screen shows **FORGET** instead.
`TEST TLS` proves the encrypted connection without logging in. `REFRESH INBOX`
authenticates with the App Password, clears that edit field, and fills the list
with up to 25 recent messages. Each row shows a `*` when unread, plus sender,
subject, and date.

Use **SHOW** beside the password field to verify the entered App Password, then
tap it again to **HIDE** it before refreshing.

Select a row and tap `OPEN` (or double-tap it) to fetch the chosen message.
The full-screen reader shows its sender, subject, date, and a scrollable
plain-text body. When only HTML is available, the reader labels its simplified
text conversion. The App Password is never written to logs. When
**REMEMBER ACCOUNT** is checked, it is stored only in the hidden app-local
encrypted account file; otherwise it remains only in memory until ReviveCE
closes. **FORGET** deletes the saved file and clears the in-memory credentials.
The password field is cleared after refresh, but do not re-enter it for `OPEN`,
`COMPOSE`, or another refresh of the same address: the active in-memory session
is reused. If a message cannot be read, the IMAP
row now states the specific test outcome, such as `MESSAGE TEXT TOO LARGE` or
`MESSAGE FORMAT NOT SUPPORTED`, alongside its diagnostic code. In the reader,
tap **LOAD MORE** when it is enabled to expand a large message safely.
Tap **REPLY** in the reader to open compose with the sender and a bounded `Re:`
subject prefilled.

After a successful inbox refresh, tap **COMPOSE** to enter a recipient, an
ASCII subject, and plain-text body, then tap **SEND**. The same temporary
in-memory App Password is reused; the compose form and its message are not
written to disk or logs. `SENT. Gmail accepted the message.` means Gmail's SMTP
server accepted it for delivery, not that a recipient has read it.

Tap **WEB GET** to open the HTTPS test screen. Enter an `https://` URL and tap
**GET**. The response view shows at most 32 KiB and labels a truncated result.

Tap **FEEDS** to choose an HTTPS RSS or Atom feed. **OPEN RSS** clears the old
list, then displays up to 25 item titles and dates after a verified response.
The app currently does not save feeds or open individual articles.

## Layout

```text
ReviveTLS.sln             Visual Studio 2008 solution
.github/workflows/        Canonical ephemeral CI build
ci/                       ARMV4I bootstrap, build, and PE verification
revivece/
  app/                    WinCE application and UI
  common/                 diagnostics
  feeds/                  bounded RSS and Atom display parser
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
