$ErrorActionPreference = 'Stop'

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$projectPath = Join-Path $repositoryRoot 'revivece\ReviveTLS.vcproj'

$requiredFiles = @(
    '.github\workflows\toolchain-test.yml',
    'ci\validate.sh',
    'ci\build-hello.sh',
    'ci\build-revivetls.sh',
    'ci\build-wolfssl.sh',
    'ci\verify-ce-pe.sh',
    'ci\verify-pe.ps1',
    'ci\hello\hello.c',
    'revivece\crypto\wolfssl\user_settings.h'
)
foreach ($requiredFile in $requiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $repositoryRoot $requiredFile) `
        -PathType Leaf)) {
        throw "Required CI file is missing: $requiredFile"
    }
}

$workflow = Get-Content -Raw -LiteralPath `
    (Join-Path $repositoryRoot '.github\workflows\toolchain-test.yml')
if ($workflow -notmatch 'runs-on:\s*ubuntu-22\.04') {
    throw 'Toolchain workflow must pin the ubuntu-22.04 runner.'
}
if ($workflow -match 'windows-latest') {
    throw 'Toolchain workflow must not use a moving windows-latest runner.'
}
if ($workflow -notmatch 'windows-ce-build-environment-arm@sha256:[0-9a-f]{64}') {
    throw 'Toolchain workflow must pin the CeGCC container by digest.'
}
if ($workflow -match 'REVIVECE_TOOLCHAIN_ARCHIVE') {
    throw 'Canonical CI must not require a private toolchain archive.'
}
if ($workflow -notmatch 'verify-ce-pe\.sh') {
    throw 'Toolchain workflow does not verify the output PE headers.'
}
if ($workflow -notmatch 'build-revivetls\.sh' -or
    $workflow -notmatch 'build-wolfssl\.sh' -or
    $workflow -notmatch 'ReviveTLS-M2-WM6-ARMV4I') {
    throw 'Toolchain workflow does not build and upload the ReviveTLS M2 artifact.'
}
if ($workflow -notmatch 'ac01707f552c611fbd135cc723b2682b3e7f80f2') {
    throw 'wolfSSL dependency is not pinned to the reviewed 5.9.2 release commit.'
}

$helloBuild = Get-Content -Raw -LiteralPath `
    (Join-Path $repositoryRoot 'ci\build-hello.sh')
if ($helloBuild -match '-mwindows') {
    throw "CeGCC for Windows CE does not support desktop MinGW's -mwindows flag."
}
if ($helloBuild -notmatch '-Wl,--subsystem,9:5\.2') {
    throw 'CeGCC build must select Windows CE GUI subsystem 9, version 5.2.'
}

$reviveTlsBuild = Get-Content -Raw -LiteralPath `
    (Join-Path $repositoryRoot 'ci\build-revivetls.sh')
if ($reviveTlsBuild -match '-mwindows') {
    throw "ReviveTLS must not use desktop MinGW's -mwindows flag."
}
if ($reviveTlsBuild -notmatch '-Wl,--subsystem,9:5\.2') {
    throw 'ReviveTLS must select Windows CE GUI subsystem 9, version 5.2.'
}
if ($reviveTlsBuild -notmatch 'libwolfssl\.a') {
    throw 'ReviveTLS CI build does not link the pinned wolfSSL library.'
}
foreach ($requiredSource in @(
    'app/main.cpp',
    'app/ui.cpp',
    'common/log.cpp',
    'net/socket.cpp',
    'net/tls.cpp'
)) {
    if ($reviveTlsBuild -notmatch [regex]::Escape("revivece/$requiredSource")) {
        throw "ReviveTLS CI build omits revivece/$requiredSource."
    }
}

$wolfSslSettings = Get-Content -Raw -LiteralPath `
    (Join-Path $repositoryRoot 'revivece\crypto\wolfssl\user_settings.h')
foreach ($requiredSetting in @(
    'WOLFSSL_USER_IO',
    'NO_WOLFSSL_SERVER',
    'NO_OLD_TLS',
    'HAVE_SNI',
    'HAVE_SUPPORTED_CURVES',
    'HAVE_AESGCM',
    'HAVE_ECC',
    'WC_RSA_BLINDING',
    'NOMINMAX',
    'WOLFSSL_GENERAL_ALIGNMENT'
)) {
    if ($wolfSslSettings -notmatch [regex]::Escape($requiredSetting)) {
        throw "wolfSSL security setting is missing: $requiredSetting."
    }
}
if ($wolfSslSettings -notmatch '#define ALIGN64\s+WOLFSSL_ALIGN\(8\)') {
    throw 'wolfSSL alignment must be capped for CeGCC PE/COFF output.'
}
if ($wolfSslSettings -notmatch '#include <time\.h>') {
    throw 'wolfSSL Windows CE build must expose time_t through time.h.'
}

[xml]$project = Get-Content -Raw -LiteralPath $projectPath
if ($project.VisualStudioProject.Version -ne '9.00') {
    throw 'ReviveTLS.vcproj is not a Visual Studio 2008 project.'
}

$platforms = @($project.VisualStudioProject.Platforms.Platform | ForEach-Object { $_.Name })
if ($platforms -notcontains 'Windows Mobile 6 Professional SDK (ARMV4I)') {
    throw 'The ARMV4I Windows Mobile 6 Professional target is missing.'
}

$projectDirectory = Split-Path -Parent $projectPath
$projectFiles = @($project.SelectNodes('//File') | ForEach-Object {
    $_.RelativePath.TrimStart('.\')
})
foreach ($relativePath in $projectFiles) {
    $fullPath = Join-Path $projectDirectory $relativePath
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "Project file is missing: $relativePath"
    }
}

$sourceFiles = Get-ChildItem -LiteralPath (Join-Path $repositoryRoot 'revivece') `
    -Recurse -File -Include *.cpp,*.h | Where-Object {
        $_.FullName -notlike '*\tests\*'
    }
$source = ($sourceFiles | Get-Content -Raw) -join "`n"

$forbiddenPatterns = @(
    'InternetOpen',
    'InternetConnect',
    'HttpOpenRequest',
    'SECURITY_FLAG_IGNORE_',
    'SSL_VERIFY_NONE',
    'plaintext fallback'
)
foreach ($pattern in $forbiddenPatterns) {
    if ($source -match [regex]::Escape($pattern)) {
        throw "Forbidden insecure API or pattern found in source: $pattern"
    }
}

$requiredPatterns = @(
    'ReviveNetConnect',
    'REVIVE_TLS_NOT_AVAILABLE',
    'WOLFSSL_USER_SETTINGS',
    'wolfSSL_check_domain_name'
)
$allText = $source + "`n" + (Get-Content -Raw -LiteralPath `
    (Join-Path $repositoryRoot 'docs\WOLFSSL_PORT.md'))
foreach ($pattern in $requiredPatterns) {
    if ($allText -notmatch [regex]::Escape($pattern)) {
        throw "Required security contract is missing: $pattern"
    }
}

Write-Host 'ReviveCE repository validation passed.' -ForegroundColor Green
