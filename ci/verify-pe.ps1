[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Path
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$resolvedPath = (Resolve-Path -LiteralPath $Path).Path
$bytes = [IO.File]::ReadAllBytes($resolvedPath)
if ($bytes.Length -lt 256) {
    throw 'Output is too small to be a valid PE executable.'
}
if ([BitConverter]::ToUInt16($bytes, 0) -ne 0x5A4D) {
    throw 'Output does not have an MZ header.'
}

$peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
if ($peOffset -lt 0 -or $peOffset + 96 -gt $bytes.Length) {
    throw 'PE header offset is outside the executable.'
}
if ([BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x00004550) {
    throw 'Output does not have a PE signature.'
}

$machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
$optionalHeaderSize = [BitConverter]::ToUInt16($bytes, $peOffset + 20)
$characteristics = [BitConverter]::ToUInt16($bytes, $peOffset + 22)
$optionalHeader = $peOffset + 24
if ($optionalHeaderSize -lt 70 -or
    $optionalHeader + $optionalHeaderSize -gt $bytes.Length) {
    throw 'PE optional header is missing or truncated.'
}
$optionalMagic = [BitConverter]::ToUInt16($bytes, $optionalHeader)
$entryPoint = [BitConverter]::ToUInt32($bytes, $optionalHeader + 16)
$majorSubsystem = [BitConverter]::ToUInt16($bytes, $optionalHeader + 48)
$minorSubsystem = [BitConverter]::ToUInt16($bytes, $optionalHeader + 50)
$subsystem = [BitConverter]::ToUInt16($bytes, $optionalHeader + 68)

if ($machine -ne 0x01C0) {
    throw ('Wrong PE machine: expected ARM 0x01C0, found 0x{0:X4}.' -f $machine)
}
if (($characteristics -band 0x0002) -eq 0) {
    throw 'PE image is not marked executable.'
}
if ($optionalMagic -ne 0x010B) {
    throw ('Wrong optional-header format: expected PE32 0x010B, found 0x{0:X4}.' -f $optionalMagic)
}
if ($subsystem -ne 9) {
    throw "Wrong PE subsystem: expected Windows CE GUI (9), found $subsystem."
}
if ($majorSubsystem -ne 5 -or $minorSubsystem -ne 2) {
    throw "Wrong CE subsystem version: expected 5.2, found $majorSubsystem.$minorSubsystem."
}
if ($entryPoint -eq 0) {
    throw 'PE entry point is zero.'
}

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $resolvedPath).Hash.ToLowerInvariant()
$hashFile = "$resolvedPath.sha256"
Set-Content -LiteralPath $hashFile -Encoding ASCII `
    -Value "$hash  HelloWorld.exe"

$summary = @"
### WM6 toolchain proof

- Machine: ARM (0x01c0)
- Subsystem: Windows CE GUI (9)
- Subsystem version: 5.2
- SHA-256: $hash
"@
Write-Host $summary
if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_STEP_SUMMARY)) {
    Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value $summary
}
