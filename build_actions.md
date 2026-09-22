Yes. I’d make **GitHub Actions the canonical ReviveCE build machine** and treat your computer only as a Git client/browser. GitHub-hosted jobs start in a fresh VM and are discarded after the job, so this fits the “nothing installed locally” goal well. `windows-2022` is available today, but it contains Visual Studio 2022 rather than VS2008, so the legacy toolchain has to be supplied by the workflow. ([GitHub Docs][1])

## ReviveCE GitHub-only build plan

### Phase 1 — Build the repository around CI

Use roughly:

```text
ReviveCE/
├── .github/
│   └── workflows/
│       ├── toolchain-test.yml
│       └── build.yml
│
├── ci/
│   ├── install-toolchain.ps1
│   ├── setup-wince-env.cmd
│   ├── build-wolfssl.cmd
│   ├── build-revivetls.cmd
│   ├── build-revivece.cmd
│   └── package-cab.cmd
│
├── app/
├── net/
├── mail/
├── feeds/
├── storage/
├── crypto/
├── installer/
└── tests/
```

The CI scripts should contain **all build knowledge**. The GitHub workflow itself stays thin.

---

## Phase 2 — Use `windows-2022`

Start with:

```yaml
jobs:
  build:
    runs-on: windows-2022
```

Pin it to `windows-2022`, not `windows-latest`, because `latest` can move to a newer Windows image. GitHub currently provides both Windows Server 2022 and 2025 hosted runners. ([GitHub][2])

Every build then gets:

```text
fresh GitHub Windows VM
        ↓
legacy toolchain bootstrap
        ↓
ARMV4I compilation
        ↓
ReviveCE artifacts
        ↓
VM destroyed
```

No Visual Studio installation on your computer.

---

## Phase 3 — Supply the legacy Microsoft compiler

This is the only awkward part.

The runner already has VS2022, but ReviveCE needs the old Windows CE ARM compiler, approximately:

```text
Visual Studio 2008 C++ CE compiler
VC\ce\bin\x86_arm\cl.exe
VC\ce\bin\x86_arm\link.exe
VC\ce\include
VC\ce\lib\ARMV4I
```

Historical CE projects use exactly that kind of layout; for example, existing VS2008 CE build instructions reference `VC\ce\bin\x86_arm`, `VC\ce\include`, and ARMV4I libraries. ([GitHub][3])

You should **not put proprietary Visual Studio binaries into a public GitHub repository**.

Instead, the Action should obtain legally licensed VS2008 installation media from private storage.

For example:

```text
GitHub Secret
VS2008_INSTALLER_URL
        ↓
GitHub Action downloads installer
        ↓
silent/unattended installation
        ↓
compiler available during job
```

Alternatively, if licensing permits your particular storage arrangement, keep a prepared toolchain archive in private storage and download it during CI.

The important point is that this is still **not installed on your PC**.

---

## Phase 4 — Install the WM6 SDK inside Actions

This part is easier because Microsoft still publishes:

**Windows Mobile 6 Professional SDK Refresh**

and the download contains the headers, libraries, emulator files and tools needed for Windows Mobile 6 development. ([Microsoft][4])

Your bootstrap script becomes conceptually:

```powershell
# install-toolchain.ps1

Install-VS2008LegacyCompiler
Install-WindowsMobile6ProfessionalSDK
Verify-ARMV4IToolchain
```

The exact silent-install switches should be tested in CI rather than assumed.

---

## Phase 5 — First workflow builds nothing except Hello World

This is critical.

Do **not** start with wolfSSL.

Your first Actions goal should be:

```text
windows-2022
      ↓
VS2008 CE compiler available
      ↓
WM6 SDK available
      ↓
compile hello.cpp for ARMV4I
      ↓
link Windows CE executable
      ↓
HelloWorld.exe
```

Then upload:

```text
HelloWorld.exe
```

as a GitHub Actions artifact.

Success means:

> GitHub can produce a native HTC Touch Pro binary.

Only after this passes do you continue.

---

## Phase 6 — Explicitly create the CE environment

I would avoid depending too heavily on Visual Studio's IDE integration.

Create a script such as:

```bat
@echo off

set VS9=C:\Program Files (x86)\Microsoft Visual Studio 9.0
set WM6=C:\Program Files (x86)\Windows Mobile 6 SDK

set PATH=%VS9%\VC\ce\bin\x86_arm;%PATH%

set INCLUDE=%VS9%\VC\ce\include;...
set LIB=%VS9%\VC\ce\lib\ARMV4I;...

set TARGETCPU=ARMV4I
```

The exact WM6 SDK paths get discovered during the initial CI experiments and then pinned.

That lets you call:

```text
cl.exe
link.exe
rc.exe
```

directly rather than requiring the VS2008 GUI.

That's much better for CI.

---

## Phase 7 — Add a toolchain verification job

Before every real build:

```bat
where cl.exe
where link.exe

cl.exe 2>&1
link.exe 2>&1
```

Then compile a tiny source file.

Also inspect the resulting PE:

```text
machine = ARM
subsystem = Windows CE
```

If it isn't ARM, fail the Action immediately.

This prevents accidentally building an x86 Windows executable.

---

## Phase 8 — Build `ReviveTLS.exe`

Now move to your actual M0–M3 path.

The Action builds:

```text
ReviveTLS.exe
```

with:

```text
WinMain
Winsock
DNS
TCP
wolfSSL
CA bundle
TLS validation
SNI
hostname verification
```

This corresponds directly to the critical TLS-proof stage in your ReviveCE MVP.

The result should be:

```text
artifacts/
└── ReviveTLS.exe
```

You download that from GitHub and copy it to the Touch Pro.

---

# Phase 9 — Build wolfSSL separately

Have CI pin one exact wolfSSL version:

```text
third_party/
└── wolfssl/
```

Preferably as either:

```text
git submodule
```

or a pinned commit.

Never build against arbitrary `master`.

For example:

```text
wolfSSL commit ABC123...
```

Then:

```text
build-wolfssl.cmd
        ↓
ARMV4I compiler
        ↓
wolfssl.lib / wolfssl.dll
```

Use the minimal features from your MVP:

```text
TLS client
TLS 1.2
X.509
SHA-256
AES
RSA
ECC
SNI
hostname verification
certificate validation
RNG
```

Disable unnecessary components.

---

## Phase 10 — Cache only safe reusable material

GitHub caches can speed things up, but remember that every hosted job starts from a fresh VM. ([GitHub Docs][5])

Good things to cache:

```text
wolfSSL intermediate objects
downloaded public SDK installer
generated CA bundle
dependency archives
```

Potentially problematic:

```text
whole installed Visual Studio
registry-dependent installations
credentials
license material
```

GitHub specifically warns against putting secrets in Actions caches. ([GitHub Docs][6])

So I would initially accept the slower clean install until everything works.

Optimize afterward.

---

# Phase 11 — Build the complete ReviveCE app

Once TLS passes on the physical phone:

```text
wolfSSL
   ↓
net/
   ↓
mail/
feeds/
storage/
   ↓
app/
   ↓
ReviveCE.exe
```

CI output:

```text
dist/
├── ReviveCE.exe
├── ReviveTLS.exe
├── wolfssl.dll
├── cacert.dat
└── ReviveCE.cab
```

Or, if wolfSSL is statically linked later:

```text
dist/
├── ReviveCE.exe
├── ReviveTLS.exe
├── cacert.dat
└── ReviveCE.cab
```

---

# Phase 12 — CAB packaging

Have CI generate:

```text
ReviveCE.inf
    ↓
CAB tooling
    ↓
ReviveCE.cab
```

The CAB should install approximately:

```text
\Program Files\ReviveCE\
    ReviveCE.exe
    wolfssl.dll
    cacert.dat
```

and create the application data location when needed.

Your actual release artifact becomes:

```text
ReviveCE-0.1-armv4i.cab
```

---

# Phase 13 — GitHub Actions artifact upload

End every successful build with:

```yaml
- name: Upload ReviveCE
  uses: actions/upload-artifact@v4
  with:
    name: ReviveCE-WM6-ARMV4I
    path: dist/
```

So your workflow becomes:

```text
git push
   ↓
Actions
   ↓
bootstrap VS2008 CE tools
   ↓
bootstrap WM6 SDK
   ↓
build wolfSSL ARMV4I
   ↓
build ReviveTLS
   ↓
build ReviveCE
   ↓
build CAB
   ↓
upload artifact
```

Then from GitHub you download:

```text
ReviveCE-WM6-ARMV4I.zip
```

---

# Phase 14 — Releases

When you create:

```text
v0.1.0
```

GitHub Actions can additionally publish:

```text
ReviveCE-0.1.0-ARMV4I.cab
ReviveCE-0.1.0-ARMV4I.zip
SHA256SUMS.txt
```

to the GitHub Release automatically.

Normal pushes just generate temporary artifacts.

Tags generate releases.

---

# Phase 15 — CI pipeline structure

I'd ultimately use these jobs:

```text
             ┌─ toolchain-test
             │
push ────────┤
             ↓
         wolfssl
             ↓
         ReviveTLS
             ↓
         ReviveCE
             ↓
          CAB
             ↓
        validation
             ↓
         artifact
             ↓
      release (tag only)
```

But initially keep everything in **one job** because separate jobs run on separate fresh VMs.

That's important.

If you split these immediately:

```text
job 1: install VS2008
job 2: compile wolfSSL
```

job 2 does **not** inherit job 1's VM.

So for MVP CI:

```yaml
jobs:
  build:
    runs-on: windows-2022
    steps:
      - checkout
      - install toolchain
      - build wolfSSL
      - build ReviveTLS
      - build ReviveCE
      - build CAB
      - verify
      - upload
```

Much simpler.

---

# The first `toolchain-test.yml`

I would start with something like this skeleton:

```yaml
name: WM6 Toolchain Test

on:
  workflow_dispatch:
  push:
    paths:
      - "ci/**"
      - ".github/workflows/toolchain-test.yml"

jobs:
  test-armv4i:
    runs-on: windows-2022

    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Bootstrap legacy toolchain
        shell: powershell
        run: |
          .\ci\install-toolchain.ps1

      - name: Setup ARMV4I environment
        shell: cmd
        run: |
          call ci\setup-wince-env.cmd
          where cl
          where link

      - name: Compile ARM HelloWorld
        shell: cmd
        run: |
          call ci\setup-wince-env.cmd
          call ci\build-hello.cmd

      - name: Verify output
        shell: powershell
        run: |
          if (!(Test-Path "dist\HelloWorld.exe")) {
              throw "ARMV4I executable was not produced"
          }

      - name: Upload executable
        uses: actions/upload-artifact@v4
        with:
          name: WM6-ARMV4I-HelloWorld
          path: dist\HelloWorld.exe
```

**Don't write the full ReviveCE CI before this workflow passes.**

---

# The milestone sequence I recommend

```text
CI0
GitHub runner starts
        ✓

CI1
VS2008 ARMV4I cl.exe executes
        ✓

CI2
WM6 SDK headers/libraries found
        ✓

CI3
HelloWorld.exe produced
        ✓

CI4
HelloWorld.exe launches on Touch Pro
        ✓

CI5
wolfSSL ARM library builds
        ✓

CI6
ReviveTLS.exe builds
        ✓

CI7
ReviveTLS works over Wi-Fi on Touch Pro
        ✓

CI8
IMAP works
        ✓

CI9
SMTP works
        ✓

CI10
HTTPS/RSS works
        ✓

CI11
ReviveCE.cab builds automatically
        ✓

CI12
tag → GitHub Release
```

The **physical-phone test remains manual**. GitHub can compile and package the software, but it obviously cannot verify that a TLS handshake really succeeds on your HTC Touch Pro.

Everything before and after that can be CI-controlled.

### One thing we need to solve first

The Windows Mobile 6 SDK itself is still officially downloadable from Microsoft. ([Microsoft][4])

The blocker to solve in the first CI experiment is **how we legally and reliably provide the VS2008 CE/ARMV4I compiler to the ephemeral GitHub runner**. Once that is solved, the rest is straightforward automation.

I’d therefore build **only the `toolchain-test.yml` first**, not ReviveCE itself. If it can produce one genuine ARMV4I `HelloWorld.exe`, we have proven that the entire GitHub-only strategy is viable.

I’ve also surfaced the GitHub connection for you; if you connect it, I can work directly with the repository on a subsequent turn instead of you manually copying workflow files.

[1]: https://docs.github.com/en/actions/reference/runners/github-hosted-runners?utm_source=chatgpt.com "GitHub-hosted runners reference - GitHub Docs"
[2]: https://github.com/actions/runner-images/blob/main/README.md?utm_source=chatgpt.com "runner-images/README.md at main · actions/runner-images · GitHub"
[3]: https://github.com/harbour/core/blob/master/README.md?utm_source=chatgpt.com "core/README.md at master · harbour/core · GitHub"
[4]: https://www.microsoft.com/en-us/download/details.aspx?id=6135&quot=&utm_source=chatgpt.com "Download Windows Mobile 6 Professional and Standard Software Development Kits Refresh from Official Microsoft Download Center"
[5]: https://docs.github.com/en/actions/how-tos/manage-runners/github-hosted-runners/use-github-hosted-runners?utm_source=chatgpt.com "Using GitHub-hosted runners - GitHub Docs"
[6]: https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching?utm_source=chatgpt.com "Dependency caching reference - GitHub Docs"
