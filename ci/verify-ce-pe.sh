#!/usr/bin/env bash
set -euo pipefail

file_path="${1:?usage: verify-ce-pe.sh path/to/application.exe}"
test -f "${file_path}"
file_name="$(basename "${file_path}")"

read_u16() {
    local offset="$1"
    local values
    read -r -a values <<<"$(od -An -tu1 -j "${offset}" -N 2 "${file_path}")"
    printf '%u\n' "$((values[0] + (values[1] << 8)))"
}

read_u32() {
    local offset="$1"
    local values
    read -r -a values <<<"$(od -An -tu1 -j "${offset}" -N 4 "${file_path}")"
    printf '%u\n' "$((values[0] + (values[1] << 8) + \
        (values[2] << 16) + (values[3] << 24)))"
}

file_size="$(stat -c '%s' "${file_path}")"
if (( file_size < 256 )); then
    echo "ERROR: Output is too small to be a PE executable." >&2
    exit 1
fi

mz_signature="$(read_u16 0)"
if (( mz_signature != 0x5a4d )); then
    echo "ERROR: Output does not have an MZ header." >&2
    exit 1
fi

pe_offset="$(read_u32 60)"
if (( pe_offset < 0 || pe_offset + 96 > file_size )); then
    echo "ERROR: PE header offset is outside the executable." >&2
    exit 1
fi
if (( $(read_u32 "${pe_offset}") != 0x00004550 )); then
    echo "ERROR: Output does not have a PE signature." >&2
    exit 1
fi

machine="$(read_u16 $((pe_offset + 4)))"
optional_size="$(read_u16 $((pe_offset + 20)))"
characteristics="$(read_u16 $((pe_offset + 22)))"
optional_header=$((pe_offset + 24))

if (( optional_size < 70 || optional_header + optional_size > file_size )); then
    echo "ERROR: PE optional header is missing or truncated." >&2
    exit 1
fi

optional_magic="$(read_u16 "${optional_header}")"
entry_point="$(read_u32 $((optional_header + 16)))"
major_subsystem="$(read_u16 $((optional_header + 48)))"
minor_subsystem="$(read_u16 $((optional_header + 50)))"
subsystem="$(read_u16 $((optional_header + 68)))"

if (( machine != 0x01c0 )); then
    printf 'ERROR: Expected ARM machine 0x01c0, found 0x%04x.\n' "${machine}" >&2
    exit 1
fi
if (( (characteristics & 0x0002) == 0 )); then
    echo "ERROR: PE image is not marked executable." >&2
    exit 1
fi
if (( optional_magic != 0x010b )); then
    printf 'ERROR: Expected PE32 magic 0x010b, found 0x%04x.\n' "${optional_magic}" >&2
    exit 1
fi
if (( subsystem != 9 )); then
    echo "ERROR: Expected Windows CE GUI subsystem 9, found ${subsystem}." >&2
    exit 1
fi
if (( major_subsystem != 5 || minor_subsystem != 2 )); then
    echo "ERROR: Expected CE subsystem 5.2, found ${major_subsystem}.${minor_subsystem}." >&2
    exit 1
fi
if (( entry_point == 0 )); then
    echo "ERROR: PE entry point is zero." >&2
    exit 1
fi

hash="$(sha256sum "${file_path}" | awk '{print $1}')"
printf '%s  %s\n' "${hash}" "${file_name}" >"${file_path}.sha256"

summary=$(cat <<EOF
### Windows CE executable: ${file_name}

- Machine: ARM (0x01c0)
- Subsystem: Windows CE GUI (9)
- Subsystem version: 5.2
- SHA-256: ${hash}
EOF
)
printf '%s\n' "${summary}"
if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
    printf '%s\n' "${summary}" >>"${GITHUB_STEP_SUMMARY}"
fi
