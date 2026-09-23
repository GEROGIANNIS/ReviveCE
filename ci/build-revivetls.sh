#!/usr/bin/env bash
set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output_directory="${repository_root}/dist"
output_file="${output_directory}/ReviveTLS.exe"
wolfssl_root="${repository_root}/third_party/wolfssl"
wolfssl_library="${repository_root}/obj/wolfssl/libwolfssl.a"
wolfssl_configuration="${repository_root}/revivece/crypto/wolfssl"
ca_bundle="${repository_root}/revivece/crypto/certs/google-roots.pem"

compiler="arm-mingw32ce-g++"
command -v "${compiler}" >/dev/null
mkdir -p "${output_directory}"
if [[ ! -s "${wolfssl_library}" ]]; then
    echo "ERROR: wolfSSL library is missing; run ci/build-wolfssl.sh first." >&2
    exit 1
fi
if [[ ! -s "${ca_bundle}" ]]; then
    echo "ERROR: Google CA bundle is missing." >&2
    exit 1
fi

sources=(
    "${repository_root}/revivece/app/main.cpp"
    "${repository_root}/revivece/app/ui.cpp"
    "${repository_root}/revivece/common/log.cpp"
    "${repository_root}/revivece/mail/imap.cpp"
    "${repository_root}/revivece/mail/smtp.cpp"
    "${repository_root}/revivece/net/socket.cpp"
    "${repository_root}/revivece/net/tls.cpp"
    "${repository_root}/revivece/net/http.cpp"
)

echo "Compiling ReviveTLS M7 for Windows CE ARM..."
"${compiler}" \
    -std=gnu++98 \
    -Os \
    -Wall \
    -Wextra \
    -march=armv4t \
    -mthumb-interwork \
    -fno-exceptions \
    -fno-rtti \
    -D_WIN32_WCE=0x0502 \
    -DUNDER_CE \
    -DWINCE \
    -D_WINDOWS \
    -DUNICODE \
    -D_UNICODE \
    -DWIN32_PLATFORM_PSPC \
    -DREVIVECE_WITH_WOLFSSL \
    -DWOLFSSL_USER_SETTINGS \
    -I"${wolfssl_configuration}" \
    -I"${wolfssl_root}" \
    -ffunction-sections \
    -fdata-sections \
    -Wl,--gc-sections \
    -Wl,--subsystem,9:5.2 \
    -static-libgcc \
    -static-libstdc++ \
    -s \
    -o "${output_file}" \
    "${sources[@]}" \
    "${wolfssl_library}" \
    -lws2 \
    -lm

test -s "${output_file}"
cp "${ca_bundle}" "${output_directory}/google-roots.pem"
test -s "${output_directory}/google-roots.pem"
echo "Built ${output_file}"
