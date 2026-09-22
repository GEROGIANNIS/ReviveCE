#!/usr/bin/env bash
set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output_directory="${repository_root}/dist"
output_file="${output_directory}/ReviveTLS.exe"

compiler="arm-mingw32ce-g++"
command -v "${compiler}" >/dev/null
mkdir -p "${output_directory}"

sources=(
    "${repository_root}/revivece/app/main.cpp"
    "${repository_root}/revivece/app/ui.cpp"
    "${repository_root}/revivece/common/log.cpp"
    "${repository_root}/revivece/net/socket.cpp"
    "${repository_root}/revivece/net/tls.cpp"
)

echo "Compiling ReviveTLS M1 for Windows CE ARM..."
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
    -ffunction-sections \
    -fdata-sections \
    -Wl,--gc-sections \
    -Wl,--subsystem,9:5.2 \
    -static-libgcc \
    -static-libstdc++ \
    -s \
    -o "${output_file}" \
    "${sources[@]}" \
    -lws2

test -s "${output_file}"
echo "Built ${output_file}"
