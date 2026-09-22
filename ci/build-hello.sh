#!/usr/bin/env bash
set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_file="${repository_root}/ci/hello/hello.c"
output_directory="${repository_root}/dist"
output_file="${output_directory}/HelloWorld.exe"

compiler="arm-mingw32ce-gcc"
command -v "${compiler}" >/dev/null
mkdir -p "${output_directory}"

echo "Compiling HelloWorld for Windows CE ARM..."
"${compiler}" \
    -Os \
    -Wall \
    -Wextra \
    -march=armv4t \
    -mthumb-interwork \
    -mwindows \
    -D_WIN32_WCE=0x0502 \
    -DUNDER_CE \
    -DWINCE \
    -DUNICODE \
    -D_UNICODE \
    -DWIN32_PLATFORM_PSPC \
    -ffunction-sections \
    -fdata-sections \
    -Wl,--gc-sections \
    -Wl,--subsystem,windowsce:5.02 \
    -s \
    -o "${output_file}" \
    "${source_file}"

test -s "${output_file}"
echo "Built ${output_file}"
