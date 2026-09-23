# ReviveCE CI toolchain

The first workflow proves that GitHub Actions can produce a native Windows CE
ARM executable without a local development environment or private compiler
archive.

## Toolchain

The job runs inside ENLYZE's public Windows CE build container:

```text
ghcr.io/enlyze/windows-ce-build-environment-arm
```

The image contains the open-source CeGCC 9.3.0 ARM toolchain. Its source is
available at:

- https://github.com/enlyze/ghcr-windows-ce-build-environment
- https://github.com/enlyze/cegcc-build

The workflow references the container by immutable SHA-256 digest rather than
the moving `latest` tag. No repository secrets are required.

CeGCC's `arm-mingw32ce` target builds native Windows CE applications against
the Windows CE API. It does not introduce the `cegcc.dll` portability runtime.

## Workflow result

Run **WM6 ARMV4I Toolchain Test** from the Actions tab. A successful run:

1. starts an `ubuntu-22.04` GitHub runner;
2. enters the digest-pinned CeGCC ARM container;
3. checks out wolfSSL 5.9.2 at full commit
   `ac01707f552c611fbd135cc723b2682b3e7f80f2`;
4. builds a static Windows CE ARM wolfSSL library, `ci/hello/hello.c`, and the
   M7 `ReviveTLS.exe` application and its reviewed Google CA bundle;
5. parses both binary PE headers and requires ARM machine `0x01c0`, PE32,
   Windows CE GUI subsystem `9`, and subsystem version `5.2`;
6. uploads `WM6-ARMV4I-HelloWorld` and `ReviveTLS-M7-WM6-ARMV4I`; the latter
   contains the executable, its SHA-256 checksum, and `google-roots.pem`.

CI0-CI3 pass when the artifact is uploaded. CI4 passes only after the downloaded
executable launches on the physical HTC Touch Pro.

The Visual Studio 2008 project remains available as a secondary compatibility
target, but it is not required by the canonical CI path.
