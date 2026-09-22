$ErrorActionPreference = 'Stop'

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$projectPath = Join-Path $repositoryRoot 'revivece\ReviveTLS.vcproj'

$requiredFiles = @(
    '.github\workflows\toolchain-test.yml',
    'ci\validate.sh',
    'ci\build-hello.sh',
    'ci\verify-ce-pe.sh',
    'ci\verify-pe.ps1',
    'ci\hello\hello.c'
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

$helloBuild = Get-Content -Raw -LiteralPath `
    (Join-Path $repositoryRoot 'ci\build-hello.sh')
if ($helloBuild -match '-mwindows') {
    throw "CeGCC for Windows CE does not support desktop MinGW's -mwindows flag."
}
if ($helloBuild -notmatch '-Wl,--subsystem,9:5\.2') {
    throw 'CeGCC build must select Windows CE GUI subsystem 9, version 5.2.'
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
