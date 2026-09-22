[CmdletBinding()]
param(
    [string]$ArchiveUrl = $env:REVIVECE_TOOLCHAIN_ARCHIVE_URL,
    [string]$ArchiveSha256 = $env:REVIVECE_TOOLCHAIN_ARCHIVE_SHA256
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Get-FirstMatchingFile {
    param(
        [System.IO.FileInfo[]]$Candidates,
        [string[]]$PreferredPatterns,
        [string]$Description
    )

    foreach ($pattern in $PreferredPatterns) {
        $match = $Candidates | Where-Object {
            $_.FullName -match $pattern
        } | Select-Object -First 1
        if ($null -ne $match) {
            return $match
        }
    }

    throw "Could not locate $Description in the private toolchain archive."
}

function Add-UniqueDirectory {
    param(
        [System.Collections.ArrayList]$List,
        [string]$Directory
    )

    if (-not [string]::IsNullOrWhiteSpace($Directory) -and
        -not $List.Contains($Directory)) {
        [void]$List.Add($Directory)
    }
}

if ([string]::IsNullOrWhiteSpace($ArchiveUrl)) {
    throw @'
REVIVECE_TOOLCHAIN_ARCHIVE_URL is not configured. Add the private archive URL
as a GitHub Actions secret; see ci/README.md. Proprietary tools are never
downloaded from an untrusted public source or committed to this repository.
'@
}
if ($ArchiveSha256 -notmatch '^[0-9a-fA-F]{64}$') {
    throw 'REVIVECE_TOOLCHAIN_ARCHIVE_SHA256 must be exactly 64 hexadecimal characters.'
}

$temporaryRoot = if ([string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) {
    Join-Path ([IO.Path]::GetTempPath()) 'revivece-ci'
} else {
    Join-Path $env:RUNNER_TEMP 'revivece-ci'
}
$archivePath = Join-Path $temporaryRoot 'licensed-toolchain.zip'
$extractRoot = Join-Path $temporaryRoot 'toolchain'
$environmentFile = Join-Path $PSScriptRoot 'toolchain-env.cmd'

New-Item -ItemType Directory -Force -Path $temporaryRoot | Out-Null
Write-Host 'Downloading the private, licensed toolchain archive.'
Invoke-WebRequest -UseBasicParsing -Uri $ArchiveUrl -OutFile $archivePath

$actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $archivePath).Hash
if ($actualHash -ine $ArchiveSha256) {
    throw "Private toolchain checksum mismatch. Expected $ArchiveSha256 but received $actualHash."
}
Write-Host 'Private toolchain archive checksum verified.'

New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
Expand-Archive -LiteralPath $archivePath -DestinationPath $extractRoot -Force

$compilerCandidates = @(Get-ChildItem -LiteralPath $extractRoot -Recurse -File `
    -Filter 'cl.exe')
$compiler = Get-FirstMatchingFile -Candidates $compilerCandidates `
    -PreferredPatterns @(
        '[\\/]VC[\\/]ce[\\/]bin[\\/]x86_arm[\\/]cl\.exe$',
        '[\\/]x86_arm[\\/]cl\.exe$'
    ) -Description 'the Visual C++ CE ARM compiler'

$linkerPath = Join-Path $compiler.DirectoryName 'link.exe'
if (-not (Test-Path -LiteralPath $linkerPath -PathType Leaf)) {
    $linkerCandidates = @(Get-ChildItem -LiteralPath $extractRoot -Recurse `
        -File -Filter 'link.exe')
    $linker = Get-FirstMatchingFile -Candidates $linkerCandidates `
        -PreferredPatterns @(
            '[\\/]VC[\\/]ce[\\/]bin[\\/]x86_arm[\\/]link\.exe$',
            '[\\/]x86_arm[\\/]link\.exe$'
        ) -Description 'the Visual C++ CE ARM linker'
    $linkerPath = $linker.FullName
}

$windowsHeaders = @(Get-ChildItem -LiteralPath $extractRoot -Recurse -File `
    -Filter 'windows.h')
$windowsHeader = Get-FirstMatchingFile -Candidates $windowsHeaders `
    -PreferredPatterns @(
        '[\\/]PocketPC[\\/]Include[\\/]Armv4i[\\/]windows\.h$',
        '[\\/]Windows Mobile 6[^\\/]*[\\/].*[\\/]Include[\\/]Armv4i[\\/]windows\.h$',
        '[\\/]VC[\\/]ce[\\/]include[\\/]windows\.h$'
    ) -Description 'Windows Mobile ARMV4I headers'

$coreLibraries = @(Get-ChildItem -LiteralPath $extractRoot -Recurse -File `
    -Filter 'coredll.lib')
$coreLibrary = Get-FirstMatchingFile -Candidates $coreLibraries `
    -PreferredPatterns @(
        '[\\/]PocketPC[\\/]Lib[\\/]Armv4i[\\/]coredll\.lib$',
        '[\\/]Windows Mobile 6[^\\/]*[\\/].*[\\/]Lib[\\/]Armv4i[\\/]coredll\.lib$',
        '[\\/]VC[\\/]ce[\\/]lib[\\/]ARMV4I[\\/]coredll\.lib$'
    ) -Description 'the ARMV4I coredll import library'

$includeDirectories = New-Object System.Collections.ArrayList
Add-UniqueDirectory -List $includeDirectories -Directory $windowsHeader.DirectoryName
$ceInclude = Join-Path (Split-Path -Parent (Split-Path -Parent `
    $compiler.DirectoryName)) 'include'
if (Test-Path -LiteralPath $ceInclude -PathType Container) {
    Add-UniqueDirectory -List $includeDirectories -Directory $ceInclude
}

$libraryDirectories = New-Object System.Collections.ArrayList
Add-UniqueDirectory -List $libraryDirectories -Directory $coreLibrary.DirectoryName
$ceLibrary = Join-Path (Split-Path -Parent (Split-Path -Parent `
    $compiler.DirectoryName)) 'lib\ARMV4I'
if (Test-Path -LiteralPath $ceLibrary -PathType Container) {
    Add-UniqueDirectory -List $libraryDirectories -Directory $ceLibrary
}

$environmentLines = @(
    '@echo off',
    "set `"REVIVECE_CL=$($compiler.FullName)`"",
    "set `"REVIVECE_LINK=$linkerPath`"",
    "set `"REVIVECE_TOOL_BIN=$($compiler.DirectoryName)`"",
    "set `"REVIVECE_INCLUDE=$($includeDirectories -join ';')`"",
    "set `"REVIVECE_LIB=$($libraryDirectories -join ';')`"",
    'set "TARGETCPU=ARMV4I"',
    'set "PATH=%REVIVECE_TOOL_BIN%;%PATH%"',
    'set "INCLUDE=%REVIVECE_INCLUDE%;%INCLUDE%"',
    'set "LIB=%REVIVECE_LIB%;%LIB%"'
)
Set-Content -LiteralPath $environmentFile -Value $environmentLines `
    -Encoding ASCII

Write-Host "CE ARM compiler: $($compiler.FullName)"
Write-Host "CE ARM linker:   $linkerPath"
Write-Host "CE include dirs: $($includeDirectories.Count) discovered"
Write-Host "CE library dirs: $($libraryDirectories.Count) discovered"
Write-Host 'Temporary compiler environment generated.'
