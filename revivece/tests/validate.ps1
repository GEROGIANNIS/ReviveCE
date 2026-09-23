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
    'revivece\crypto\certs\google-roots.pem',
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
    $workflow -notmatch 'ReviveTLS-M6-WM6-ARMV4I' -or
    $workflow -notmatch 'google-roots\.pem') {
    throw 'Toolchain workflow does not build and upload the ReviveTLS M5 artifact.'
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

$socketSource = Get-Content -Raw -LiteralPath `
    (Join-Path $repositoryRoot 'revivece\net\socket.cpp')
if ($socketSource -match 'SO_RCVTIMEO|SO_SNDTIMEO') {
    throw 'Windows CE 5.2 rejects socket timeout options with WSAENOPROTOOPT.'
}
if ($reviveTlsBuild -notmatch 'google-roots\.pem') {
    throw 'ReviveTLS CI build does not package the Google CA bundle.'
}
$imapSource = Get-Content -Raw -LiteralPath `
    (Join-Path $repositoryRoot 'revivece\mail\imap.cpp')
foreach ($requiredImapControl in @(
    'ReviveImapFetchInbox',
    'A001 LOGIN',
    'A002 SELECT INBOX',
    'A003 UID SEARCH ALL',
    'BODY.PEEK[HEADER.FIELDS',
    'ReviveImapClearCredentials'
)) {
    if ($imapSource -notmatch [regex]::Escape($requiredImapControl)) {
        throw "M4 IMAP control is missing: $requiredImapControl."
    }
}
$messageSource = $imapSource + "`n" + (Get-Content -Raw -LiteralPath `
    (Join-Path $repositoryRoot 'revivece\mail\imap.h'))
foreach ($requiredMessageControl in @(
    'ReviveImapFetchMessage',
    'A003 UID FETCH',
    'BODY.PEEK[TEXT]',
    'BODY.PEEK[1.MIME]',
    'ReadSectionFetchCompletion',
    'REVIVE_IMAP_INITIAL_BODY_BYTES'
)) {
    if ($messageSource -notmatch [regex]::Escape($requiredMessageControl)) {
        throw "M5 message control is missing: $requiredMessageControl."
    }
}
$smtpSource = (Get-Content -Raw -LiteralPath `
    (Join-Path $repositoryRoot 'revivece\mail\smtp.cpp')) + "`n" +
    (Get-Content -Raw -LiteralPath (Join-Path $repositoryRoot 'revivece\app\ui.cpp'))
foreach ($requiredSmtpControl in @(
    'ReviveSmtpSendMessage',
    'AUTH LOGIN',
    'MAIL FROM:',
    'RCPT TO:',
    'smtp.gmail.com'
)) {
    if ($smtpSource -notmatch [regex]::Escape($requiredSmtpControl)) {
        throw "M6 SMTP control is missing: $requiredSmtpControl."
    }
}
foreach ($requiredSource in @(
    'app/main.cpp',
    'app/ui.cpp',
    'common/log.cpp',
    'net/socket.cpp',
    'net/tls.cpp',
    'mail/imap.cpp',
    'mail/smtp.cpp'
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
    'WOLFSSL_ALT_CERT_CHAINS',
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

$caBundlePath = Join-Path $repositoryRoot `
    'revivece\crypto\certs\google-roots.pem'
$certificateCount = (Select-String -LiteralPath $caBundlePath `
    -Pattern '-----BEGIN CERTIFICATE-----').Count
if ($certificateCount -ne 21) {
    throw 'Google trust bundle must contain the reviewed 21 certificates.'
}
$bundleHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $caBundlePath).Hash
if ($bundleHash -ne `
    'EC989DF46C8F4419EF2EE2517CAD7619D555E4973F3307BE697662AA2497E480') {
    throw 'Google trust bundle hash differs from the reviewed bundle.'
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
    'WOLFSSL_VERIFY_PEER',
    'wolfTLSv1_2_client_method',
    'wolfSSL_UseSNI',
    'wolfSSL_check_domain_name',
    'wolfSSL_CTX_load_verify_buffer'
)
$allText = $source + "`n" + (Get-Content -Raw -LiteralPath `
    (Join-Path $repositoryRoot 'docs\WOLFSSL_PORT.md'))
foreach ($pattern in $requiredPatterns) {
    if ($allText -notmatch [regex]::Escape($pattern)) {
        throw "Required security contract is missing: $pattern"
    }
}

Write-Host 'ReviveCE repository validation passed.' -ForegroundColor Green
