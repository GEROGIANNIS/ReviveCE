# ReviveCE CI bootstrap

The first workflow proves that an ephemeral GitHub-hosted Windows runner can
produce a native ARMV4I Windows CE executable. It deliberately does not build
wolfSSL or ReviveTLS yet.

## Private toolchain archive

Visual Studio 2008 C++ Smart Device tools are licensed Microsoft software and
must not be committed to this repository or uploaded as a workflow artifact.
Place a legally licensed toolchain archive in private storage and configure
these GitHub Actions secrets:

| Secret | Meaning |
| --- | --- |
| `REVIVECE_TOOLCHAIN_ARCHIVE_URL` | Time-limited private URL to a ZIP archive |
| `REVIVECE_TOOLCHAIN_ARCHIVE_SHA256` | Lower- or upper-case SHA-256 of that ZIP |

The checksum is mandatory. The workflow never caches or re-uploads the archive.
Prefer a short-lived read-only URL. GitHub masks secret values, but the scripts
also avoid printing the URL.

The extracted archive may contain arbitrary parent directories, but it must
contain all of the following:

```text
VC\ce\bin\x86_arm\cl.exe
VC\ce\bin\x86_arm\link.exe
VC\ce\include\...
VC\ce\lib\ARMV4I\...
...\PocketPC\Include\Armv4i\windows.h
...\PocketPC\Lib\Armv4i\coredll.lib
```

The first four paths normally come from a licensed Visual Studio 2008 install
with Visual C++ Smart Device Programmability. The PocketPC paths come from the
Windows Mobile 6 Professional SDK Refresh. Preserve their directory structure
when creating the ZIP.

`install-toolchain.ps1` discovers the files after extraction and writes a
temporary `ci\toolchain-env.cmd`. That file is ignored by Git and contains only
runner-local paths.

## Workflow result

Run **WM6 ARMV4I Toolchain Test** from the Actions tab. A successful run:

1. verifies the private archive checksum;
2. finds the CE ARM compiler, linker, headers, and libraries;
3. builds `ci/hello/hello.c` without a desktop CRT dependency;
4. parses the PE headers and requires ARM machine `0x01c0` plus Windows CE GUI
   subsystem `9`;
5. uploads `WM6-ARMV4I-HelloWorld` for installation on the HTC Touch Pro.

CI0-CI3 pass when the artifact is uploaded. CI4 passes only after the downloaded
executable launches on the physical phone.
