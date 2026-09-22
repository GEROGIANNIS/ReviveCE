#!/usr/bin/env bash
set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repository_root}"

required_files=(
    ".github/workflows/toolchain-test.yml"
    "ci/build-hello.sh"
    "ci/build-revivetls.sh"
    "ci/build-wolfssl.sh"
    "ci/verify-ce-pe.sh"
    "ci/hello/hello.c"
    "revivece/ReviveTLS.vcproj"
    "revivece/crypto/wolfssl/user_settings.h"
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
grep -q 'build-revivetls.sh' "${workflow}"
grep -q 'build-wolfssl.sh' "${workflow}"
grep -q 'ReviveTLS-M2-WM6-ARMV4I' "${workflow}"
grep -q 'ac01707f552c611fbd135cc723b2682b3e7f80f2' "${workflow}"

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
if grep -q -- '-mwindows' ci/build-revivetls.sh; then
    echo "ERROR: ReviveTLS must not use desktop MinGW's -mwindows flag." >&2
    exit 1
fi
if ! grep -q -- '-Wl,--subsystem,9:5.2' ci/build-revivetls.sh; then
    echo "ERROR: ReviveTLS must select Windows CE GUI subsystem 9, version 5.2." >&2
    exit 1
fi
if ! grep -q 'libwolfssl.a' ci/build-revivetls.sh; then
    echo "ERROR: ReviveTLS CI build does not link the pinned wolfSSL library." >&2
    exit 1
fi
for required_source in app/main.cpp app/ui.cpp common/log.cpp net/socket.cpp net/tls.cpp; do
    if ! grep -q "revivece/${required_source}" ci/build-revivetls.sh; then
        echo "ERROR: ReviveTLS CI build omits revivece/${required_source}." >&2
        exit 1
    fi
done

wolfssl_settings="revivece/crypto/wolfssl/user_settings.h"
for required_setting in \
    WOLFSSL_USER_IO \
    NO_WOLFSSL_SERVER \
    NO_OLD_TLS \
    HAVE_SNI \
    HAVE_SUPPORTED_CURVES \
    HAVE_AESGCM \
    HAVE_ECC \
    WC_RSA_BLINDING; do
    if ! grep -q "${required_setting}" "${wolfssl_settings}"; then
        echo "ERROR: wolfSSL security setting is missing: ${required_setting}." >&2
        exit 1
    fi
done

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
