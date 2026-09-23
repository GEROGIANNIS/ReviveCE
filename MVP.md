# ReviveCE MVP 0.1

## Mission

Build a native Windows Mobile 6.1 application for the HTC Touch Pro that can securely access modern Internet services without relying on Windows Mobile's obsolete TLS implementation.

The application provides two functions:

**Mail**

* Receive Gmail
* Read messages
* Send messages

**Feeds**

* Fetch HTTPS RSS/Atom feeds
* Show unread articles
* Read article summaries/content

Everything connects directly from the phone.

No proxy server.
No Raspberry Pi.
No DDNS.
No port forwarding.
No permanently running PC.

---

# Target Hardware

Initial target:

**HTC Touch Pro / HTC Raphael**

Typical hardware:

* Windows Mobile 6.1 Professional
* Windows CE 5.2 base
* ARM11 / Qualcomm MSM7201A
* 528 MHz CPU
* 288 MB RAM
* 480 × 640 VGA screen
* Wi-Fi
* 3G/HSPA
* hardware QWERTY keyboard

The application should eventually work on other ARM Windows Mobile 6.x devices, but we optimize the first version specifically for the Touch Pro.

---

# Architecture

```text
                     Internet
                        │
           ┌────────────┴──────────────┐
           │                           │
     Gmail IMAP/SMTP             HTTPS websites
           │                           │
       TLS 1.2                    TLS 1.2
           │                           │
           └────────────┬──────────────┘
                        │
                     wolfSSL
                        │
                 ReviveCE Network
                        │
            ┌───────────┴────────────┐
            │                        │
          Mail                      RSS
            │                        │
       MIME parser             RSS/Atom parser
            │                        │
            └───────────┬────────────┘
                        │
                     Local DB
                        │
                       UI
                        │
                  HTC Touch Pro
```

The critical design decision is that Windows Mobile provides only:

```text
DNS
TCP sockets
filesystem
UI
```

Windows Mobile does **not** provide TLS.

wolfSSL performs TLS completely inside ReviveCE.

---

# Phase 0 — TLS Proof

Before building ReviveCE itself, create a tiny program:

**ReviveTLS.exe**

It has one screen:

```text
ReviveTLS Test

DNS .............. OK
TCP .............. OK
TLS 1.2 .......... OK
Certificate ...... OK
Hostname .......... OK

Server:
imap.gmail.com

[ RUN TEST ]
```

Its only job is:

```text
DNS lookup
    ↓
TCP connect
    ↓
wolfSSL handshake
    ↓
verify certificate
    ↓
verify hostname
    ↓
receive server response
```

Test targets:

```text
imap.gmail.com:993

smtp.gmail.com:465

https://www.google.com/
```

Then test an HTTPS RSS endpoint.

### Phase-0 success condition

This must work on the **physical HTC Touch Pro**, over:

```text
Wi-Fi
```

and ideally:

```text
3G
```

before development continues.

If this works, the hardest technical uncertainty in the project has been solved.

---

# Networking Layer

Create one shared networking module.

```text
revive_net
│
├── dns.cpp
├── tcp.cpp
├── tls.cpp
├── http.cpp
└── certs.cpp
```

Expose a small interface:

```text
ReviveNetConnect()
ReviveTLSConnect()
ReviveTLSRead()
ReviveTLSWrite()
ReviveTLSClose()

ReviveHttpGet()
```

Everything else in the application uses this API.

Mail and RSS should never call Windows Mobile TLS APIs.

---

# TLS Configuration

For MVP:

```text
TLS 1.2
certificate validation ON
hostname validation ON
SNI ON
secure cipher suites only
plaintext fallback NEVER allowed
```

Do not provide a:

```text
"Ignore certificate errors"
```

button.

If validation fails:

```text
Connection rejected.
```

This protects the user from accidentally turning the old phone into an insecure client.

---

# Certificate Store

ReviveCE ships with its own modern CA certificate bundle.

Do not rely on the Windows Mobile root certificate store.

Architecture:

```text
ReviveCE
   │
   └── certs/
       └── roots.der / bundled CA store
```

This bundle can later be updated by installing a newer ReviveCE CAB.

That gives us control over certificate compatibility without modifying Windows Mobile itself.

---

# Gmail MVP

One Gmail account.

Configuration screen:

```text
Email:
user@gmail.com

App Password:
•••• •••• •••• ••••

Incoming:
imap.gmail.com
993

Outgoing:
smtp.gmail.com
465

[ Test Account ]
```

Authentication uses a Google App Password rather than the user's primary Google password.

Google currently allows App Passwords for eligible accounts with 2-Step Verification when the application cannot use modern Google sign-in.

---

# Gmail Features in 0.1

Implement only:

```text
✓ Connect
✓ Authenticate
✓ Inbox
✓ newest 25 messages
✓ sender
✓ subject
✓ date
✓ unread/read status
✓ plain-text message body
✓ compose
✓ reply
✓ send
✓ manual refresh
```

Do NOT implement yet:

```text
✗ attachments
✗ HTML rendering
✗ folders
✗ Gmail labels
✗ search
✗ push mail
✗ background synchronization
✗ contacts
✗ calendar
✗ conversation threading
```

Those can come later.

---

# Mail UI

Main screen:

```text
┌─────────────────────────────┐
│ ReviveCE              14:32 │
├─────────────────────────────┤
│ MAIL          FEEDS         │
├─────────────────────────────┤
│ ● Alice                     │
│   Meeting tomorrow          │
│   14:20                     │
├─────────────────────────────┤
│ ● GitHub                    │
│   Build completed           │
│   13:55                     │
├─────────────────────────────┤
│   John                      │
│   Re: weekend               │
│   12:14                     │
├─────────────────────────────┤
│ Refresh             Compose │
└─────────────────────────────┘
```

Designed for:

```text
480 × 640 portrait
touch
D-pad
hardware keyboard
```

It should remain usable without TouchFLO.

---

# Message Viewer

```text
Alice Smith
alice@example.com

Meeting tomorrow

─────────────────────────────

Hi,

Are we still meeting tomorrow
at 10?

Alice


[ Reply ]               [ Back ]
```

MVP primarily renders:

```text
text/plain
```

For multipart email:

```text
prefer text/plain
```

If only HTML exists:

```text
HTML
 ↓
very simple HTML → text converter
 ↓
display text
```

Do not embed Internet Explorer for MVP.

---

# Sending Mail

SMTP flow:

```text
connect smtp.gmail.com:465
        ↓
TLS handshake
        ↓
EHLO
        ↓
AUTH
        ↓
MAIL FROM
        ↓
RCPT TO
        ↓
DATA
        ↓
QUIT
```

Support basic:

```text
To
Subject
Body
```

Initially plain text only.

---

# RSS MVP

The feed manager stores:

```text
name
URL
last refresh
last item ID
```

Example:

```text
Feeds

● BBC News
  12 unread

● Ars Technica
  5 unread

● Hacker News
  18 unread

[ Add ]           [ Refresh ]
```

Add-feed dialog:

```text
Feed URL:

https://example.com/feed.xml

[ Test ]    [ Add ]
```

ReviveCE performs:

```text
HTTPS GET
    ↓
TLS
    ↓
HTTP response
    ↓
XML
    ↓
detect RSS/Atom
    ↓
parse entries
```

---

# RSS Formats

MVP parser supports:

```text
RSS 2.0

Atom 1.0
```

Extract:

```text
title
link
published date
description/summary
GUID/ID
```

Ignore uncommon extensions initially.

---

# Article Reader

```text
BBC News

Example headline

22 September 2026 — 14:15

─────────────────────────────

Article description or feed
content appears here.

Lorem ipsum...

[ Open URL ]             [ Back ]
```

`Open URL` can initially attempt the installed browser.

Later ReviveCE itself could gain a lightweight HTML reader.

---

# Local Storage

Keep storage deliberately simple.

```text
\Program Files\ReviveCE\
    ReviveCE.exe
    wolfssl.dll
    cacert.dat

\Application Data\ReviveCE\
    settings.dat
    mail.db
    feeds.db
```

For the first release we can use compact binary records rather than bringing in a large database engine.

Possible structure:

```text
Account
Feed
FeedItem
MailHeader
MailBody
Settings
```

---

# Credential Security

Never store the user's normal Google password.

Only store:

```text
Gmail App Password
```

For the secure version, encrypt it locally.

A practical design is:

```text
User PIN
   ↓
PBKDF2
   ↓
encryption key
   ↓
AES encrypted credential blob
```

wolfCrypt can provide the cryptographic primitives, so we don't need to trust Windows Mobile's old crypto stack.

Never write credentials into:

```text
logs
crash dumps
debug output
configuration text files
```

Add:

```text
Settings → Forget Account
```

which deletes the encrypted credential.

---

# Logging

Development version:

```text
[NET] resolving imap.gmail.com
[NET] connected
[TLS] handshake started
[TLS] TLS1.2 negotiated
[TLS] certificate valid
[IMAP] greeting received
[IMAP] authenticated
```

Never log:

```text
passwords
AUTH payloads
full private email
```

Logs are essential because debugging directly on Windows Mobile will otherwise be painful.

---

# Project Structure

```text
revivece/
│
├── app/
│   ├── main.cpp
│   ├── ui.cpp
│   ├── ui.h
│   └── resource.rc
│
├── net/
│   ├── socket.cpp
│   ├── socket.h
│   ├── tls.cpp
│   ├── tls.h
│   ├── http.cpp
│   └── http.h
│
├── mail/
│   ├── imap.cpp
│   ├── imap.h
│   ├── smtp.cpp
│   ├── smtp.h
│   ├── mime.cpp
│   └── mime.h
│
├── feeds/
│   ├── feed.cpp
│   ├── feed.h
│   ├── rss.cpp
│   └── atom.cpp
│
├── storage/
│   ├── settings.cpp
│   ├── database.cpp
│   └── credentials.cpp
│
├── crypto/
│   ├── wolfssl/
│   ├── user_settings.h
│   └── ca_bundle.dat
│
├── tests/
│
├── installer/
│   └── ReviveCE.inf
│
└── README.md
```

---

# Build Environment

Initial build system:

```text
Windows development PC / VM
        │
Visual Studio 2008
        │
Windows Mobile 6 Professional SDK
        │
ARMV4I target
        │
ReviveCE.exe
        │
CAB installer
        │
HTC Touch Pro
```

Use a real device for TLS testing.

The emulator can help with UI development but should not be considered proof that networking works correctly on the Touch Pro.

---

# wolfSSL Build

Start with the current wolfSSL source.

Create a very small configuration.

We don't need:

```text
TLS server
DTLS
TLS 1.3 initially
old SSL
old TLS
FTP
SSH
etc.
```

We need:

```text
TLS client
TLS 1.2
X.509
SHA-256
AES
ECC
RSA
SNI
hostname verification
certificate chain validation
secure RNG
```

The goal is to minimize:

```text
binary size
RAM
compile dependencies
```

without weakening security.

---

# The First Real Program

Before ReviveCE.exe:

```text
ReviveTLS.exe
```

Approximately:

```text
WinMain()
    ↓
InitializeWinsock()
    ↓
Resolve("imap.gmail.com")
    ↓
SocketConnect(993)
    ↓
wolfSSL_Init()
    ↓
LoadCABundle()
    ↓
CreateTLSContext()
    ↓
SetSNI("imap.gmail.com")
    ↓
SetHostnameVerification("imap.gmail.com")
    ↓
wolfSSL_connect()
    ↓
Read()
    ↓
DisplayResult()
```

Expected response after TLS succeeds should begin roughly as an IMAP server greeting.

At this point we know:

**Modern secure networking works on the HTC.**

That is milestone #1.

---

# Development Milestones

## M0 — Toolchain

Produce:

```text
HelloWorld.exe
```

running on the physical Touch Pro.

Success:

```text
ARM binary launches.
```

---

## M1 — TCP

ReviveTLS performs:

```text
DNS
TCP connection
```

Success:

```text
imap.gmail.com:993 socket connects.
```

---

## M2 — wolfSSL

Compile wolfSSL for the Touch Pro.

Success:

```text
wolfSSL_Init() works
without crash.
```

---

## M3 — TLS

Perform a real TLS connection.

Success:

```text
TLS handshake succeeds
certificate verifies
hostname verifies
server response received
```

**This is the project's critical milestone.**

---

## M4 — IMAP

Implement:

```text
LOGIN
SELECT INBOX
FETCH latest 25
```

Success:

```text
Touch Pro displays Gmail subjects.
```

Current implementation note: the M4 proof uses a Gmail App Password supplied
for the active ReviveCE session only. It is never persisted or logged, and is
cleared when ReviveCE closes. It performs tagged `LOGIN`, `SELECT INBOX`, `UID
SEARCH ALL`, and header-only `UID FETCH` commands over the verified M3 TLS
session, keeping the newest 25 UIDs and displaying sender/subject rows.

## M5 — Message reader

Implement MIME parser.

Success:

```text
Open email
read plain-text body
```

Current implementation note: selecting an M4 inbox row opens the M5 reader.
It establishes another verified IMAP connection, fetches headers first and a
message text section without saving attachments, then renders a scrollable
`text/plain` body. Common multipart messages expose their first text part; when
that part is HTML, it uses a compact HTML-to-text fallback.

M5.1 starts with an 8 KiB body prefix and offers **LOAD MORE** when the server
has additional text. Each tap expands the safely decoded prefix by 8 KiB, up
to 32 KiB; this avoids downloading attachments and avoids breaking quoted-
printable or Base64 content at arbitrary page boundaries.

---

## M6 — SMTP

Implement sending.

Success:

```text
Compose on HTC
send through Gmail
receive message elsewhere
```

Current implementation note: M6 provides a bounded plain-text compose form.
It makes a new verified implicit-TLS connection to `smtp.gmail.com:465`, uses
the active in-memory App Password with `AUTH LOGIN`, then sends `MAIL FROM`,
`RCPT TO`, and `DATA`. Attachments, HTML composition, drafts, and sent-mail
listing remain later milestones.

At this point:

**ReviveCE Mail MVP works.**

---

## M7 — HTTPS

Implement HTTP client over the same TLS layer.

Success:

```text
HTTPS GET returns page/feed.
```

---

## M8 — RSS/Atom

Implement parser.

Success:

```text
add HTTPS RSS URL
download
show headlines
read items
```

At this point:

**ReviveCE MVP 0.1 is complete.**

---

# MVP Definition of Done

ReviveCE 0.1 is successful when I can take the Touch Pro away from the development PC and do this:

```text
Turn on phone
      ↓
connect Wi-Fi / 3G
      ↓
open ReviveCE
      ↓
refresh Gmail
      ↓
read an email
      ↓
reply
      ↓
switch to Feeds
      ↓
refresh HTTPS feeds
      ↓
read headlines
```

with:

```text
no proxy
no home server
no DDNS
no computer
no plaintext Internet connection
```

---

# If wolfSSL Fails on CE 5.2

Do not abandon the architecture.

Try, in order:

```text
1. Patch wolfSSL CE compatibility layer.

2. Implement missing WinCE libc/time/socket
   compatibility functions.

3. Build wolfSSL with custom I/O callbacks
   directly over Winsock.

4. If necessary, test an older wolfSSL branch
   known to compile on CE5 and backport required
   security fixes/features.

5. Only if wolfSSL proves impractical:
   move TLS implementation to another
   CE-compatible library.
```

The proxy approach stays the last resort, not the primary architecture.

---

# Version Roadmap

After 0.1:

### 0.2

```text
attachments
HTML email
multiple folders
Gmail labels
multiple feeds folders
```

### 0.3

```text
background refresh
notifications
offline cache
image downloading
```

### 0.4

```text
multiple mail accounts
OPML import/export
feed auto-discovery
```

### 1.0

```text
polished Touch Pro UI
stable mail
stable feeds
installer/updater
certificate bundle update
configuration backup
```

The philosophy should remain:

> Make modern Internet protocols work on vintage hardware instead of trying to make modern servers behave like 2008 servers.
