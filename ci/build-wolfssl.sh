#!/usr/bin/env bash
set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
wolfssl_root="${repository_root}/third_party/wolfssl"
configuration_directory="${repository_root}/revivece/crypto/wolfssl"
object_directory="${repository_root}/obj/wolfssl"
library_file="${object_directory}/libwolfssl.a"

compiler="arm-mingw32ce-gcc"
archiver="arm-mingw32ce-ar"
ranlib="arm-mingw32ce-ranlib"
command -v "${compiler}" >/dev/null
command -v "${archiver}" >/dev/null
command -v "${ranlib}" >/dev/null

if [[ ! -f "${wolfssl_root}/wolfssl/ssl.h" ]]; then
    echo "ERROR: Pinned wolfSSL source is missing at ${wolfssl_root}." >&2
    exit 1
fi

rm -rf "${object_directory}"
mkdir -p "${object_directory}"

common_flags=(
    -std=gnu99
    -Os
    -march=armv4t
    -mthumb-interwork
    -fno-strict-aliasing
    -ffunction-sections
    -fdata-sections
    -D_WIN32_WCE=0x0502
    -DUNDER_CE
    -DWINCE
    -DWOLFSSL_USER_SETTINGS
    -I"${configuration_directory}"
    -I"${wolfssl_root}"
)

objects=()

compile_source() {
    local source_file="$1"
    local object_prefix="$2"
    local base_name
    local object_file

    base_name="$(basename "${source_file}" .c)"
    object_file="${object_directory}/${object_prefix}_${base_name}.o"
    "${compiler}" "${common_flags[@]}" -c "${source_file}" -o "${object_file}"
    objects+=("${object_file}")
}

echo "Compiling pinned wolfSSL for Windows CE ARM..."
while IFS= read -r -d '' source_file; do
    case "${source_file}" in
        */asn_orig.c|*/evp.c|*/evp_pk.c|*/misc.c)
            continue
            ;;
    esac
    compile_source "${source_file}" "wolfcrypt"
done < <(find "${wolfssl_root}/wolfcrypt/src" -maxdepth 1 -type f \
    -name '*.c' -print0 | sort -z)

while IFS= read -r -d '' source_file; do
    case "$(basename "${source_file}")" in
        bio.c|conf.c|pk.c|pk_ec.c|pk_rsa.c|ssl_*.c|x509.c|x509_str.c)
            continue
            ;;
    esac
    compile_source "${source_file}" "tls"
done < <(find "${wolfssl_root}/src" -maxdepth 1 -type f \
    -name '*.c' -print0 | sort -z)

if (( ${#objects[@]} == 0 )); then
    echo "ERROR: No wolfSSL sources were compiled." >&2
    exit 1
fi

"${archiver}" rcs "${library_file}" "${objects[@]}"
"${ranlib}" "${library_file}"
test -s "${library_file}"
echo "Built ${library_file} from ${#objects[@]} source files"
