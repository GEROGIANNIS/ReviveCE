#!/usr/bin/env bash
set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repository_root}"

required_files=(
    ".github/workflows/toolchain-test.yml"
    "ci/build-hello.sh"
    "ci/verify-ce-pe.sh"
    "ci/hello/hello.c"
    "revivece/ReviveTLS.vcproj"
    "revivece/net/tls.cpp"
)
for required_file in "${required_files[@]}"; do
    if [[ ! -f "${required_file}" ]]; then
        echo "ERROR: Required file is missing: ${required_file}" >&2
        exit 1
    fi
done

workflow=".github/workflows/toolchain-test.yml"
grep -Eq 'runs-on:[[:space:]]*ubuntu-22\.04' "${workflow}"
grep -Eq 'ghcr\.io/enlyze/windows-ce-build-environment-arm@sha256:[0-9a-f]{64}' "${workflow}"
grep -q 'verify-ce-pe.sh' "${workflow}"

if grep -q 'windows-latest' "${workflow}"; then
    echo "ERROR: Workflow must not use a moving windows-latest runner." >&2
    exit 1
fi
if grep -q 'REVIVECE_TOOLCHAIN_ARCHIVE' "${workflow}"; then
    echo "ERROR: Canonical CI must not require a private toolchain archive." >&2
    exit 1
fi
if grep -q -- '-mwindows' ci/build-hello.sh; then
    echo "ERROR: CeGCC for Windows CE does not support desktop MinGW's -mwindows flag." >&2
    exit 1
fi
if ! grep -q -- '-Wl,--subsystem,9:5.2' ci/build-hello.sh; then
    echo "ERROR: CeGCC build must select Windows CE GUI subsystem 9, version 5.2." >&2
    exit 1
fi

source_files=$(find revivece -path 'revivece/tests' -prune -o \
    \( -name '*.cpp' -o -name '*.h' \) -type f -print)
for forbidden in \
    InternetOpen \
    InternetConnect \
    HttpOpenRequest \
    SECURITY_FLAG_IGNORE_ \
    SSL_VERIFY_NONE; do
    if grep -Fq "${forbidden}" ${source_files}; then
        echo "ERROR: Forbidden insecure API or pattern found: ${forbidden}" >&2
        exit 1
    fi
done

echo 'ReviveCE repository validation passed.'
