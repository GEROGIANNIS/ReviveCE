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
    "revivece/crypto/certs/google-roots.pem"
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
grep -q 'ReviveTLS-M3-WM6-ARMV4I' "${workflow}"
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
grep -q 'google-roots.pem' ci/build-revivetls.sh
grep -q 'google-roots.pem' "${workflow}"

certificate_count=$(grep -c -- '-----BEGIN CERTIFICATE-----' \
    revivece/crypto/certs/google-roots.pem)
if [[ "${certificate_count}" -ne 21 ]]; then
    echo "ERROR: Google trust bundle must contain the reviewed 21 certificates." >&2
    exit 1
fi
bundle_sha256=$(sha256sum revivece/crypto/certs/google-roots.pem | cut -d' ' -f1)
if [[ "${bundle_sha256}" != \
    "ec989df46c8f4419ef2ee2517cad7619d555e4973f3307be697662aa2497e480" ]]; then
    echo "ERROR: Google trust bundle hash differs from the reviewed bundle." >&2
    exit 1
fi

wolfssl_settings="revivece/crypto/wolfssl/user_settings.h"
for required_setting in \
    WOLFSSL_USER_IO \
    NO_WOLFSSL_SERVER \
    NO_OLD_TLS \
    HAVE_SNI \
    HAVE_SUPPORTED_CURVES \
    HAVE_AESGCM \
    HAVE_ECC \
    WC_RSA_BLINDING \
    NOMINMAX \
    WOLFSSL_GENERAL_ALIGNMENT; do
    if ! grep -q "${required_setting}" "${wolfssl_settings}"; then
        echo "ERROR: wolfSSL security setting is missing: ${required_setting}." >&2
        exit 1
    fi
done
if ! grep -Eq '^#define ALIGN64[[:space:]]+WOLFSSL_ALIGN\(8\)' "${wolfssl_settings}"; then
    echo "ERROR: wolfSSL alignment must be capped for CeGCC PE/COFF output." >&2
    exit 1
fi
if ! grep -q '#include <time.h>' "${wolfssl_settings}"; then
    echo "ERROR: wolfSSL Windows CE build must expose time_t through time.h." >&2
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

tls_source="revivece/net/tls.cpp"
for required_tls_control in \
    WOLFSSL_VERIFY_PEER \
    wolfTLSv1_2_client_method \
    wolfSSL_UseSNI \
    wolfSSL_check_domain_name \
    wolfSSL_CTX_load_verify_buffer; do
    if ! grep -q "${required_tls_control}" "${tls_source}"; then
        echo "ERROR: TLS security control is missing: ${required_tls_control}." >&2
        exit 1
    fi
done

echo 'ReviveCE repository validation passed.'
