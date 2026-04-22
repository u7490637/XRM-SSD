#!/usr/bin/env bash
# Post-build hardening pass for improved/bin/*
# Copyright (c) 2026 STARGA, Inc. All rights reserved.
#
# Applied on top of the standard mindc + cargo build. Strips every
# piece of build-environment metadata that a third party could use
# to fingerprint the binary (build-id, ABI notes, GNU property notes,
# linker .comment strings). Also normalises .comment across all three
# binaries to a single MIND attribution line.
#
# Run AFTER build.sh from the improved/ directory:
#     ./protection/harden.sh
#
# Inputs (must exist):
#     bin/libxrmgov
#     bin/libmind_cpu_linux-x64.so
#     bin/xrm_mind_port
#
# Output: same files, post-processed in place, plus SHA256SUMS refreshed.

set -euo pipefail
cd "$(dirname "$0")/.."

BIN="bin"
if [[ ! -d "${BIN}" ]]; then
    echo "FATAL: ${PWD}/${BIN} missing. Run ./build.sh first." >&2
    exit 1
fi

# Detect mindc version (for the normalised .comment string)
MINDC_VER=$(mindc --version 2>/dev/null | head -1 || echo "mind 0.2.3")
COMMENT_LINE="MIND: ${MINDC_VER} (STARGA toolchain)"

# Sections to strip from every ELF we ship.
# .note.gnu.build-id  -> unique build fingerprint (per-link)
# .note.gnu.property  -> x86 ISA / IBT / SHSTK markers
# .note.ABI-tag       -> target ABI version marker
# .comment            -> rustc / ld / gcc version strings (HUGE leak in Rust binaries)
# .gnu.build.attributes -> GCC annotations (rare but present with -grecord-gcc-switches)
NUKE_SECTIONS=(
    .note.gnu.build-id
    .note.gnu.property
    .note.ABI-tag
    .comment
    .gnu.build.attributes
)

# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# Zero build-path strings in .rodata. rustc and mindc both bake panic-location
# file paths into read-only data ("/rustc/<hash>/library/...", "/home/n/mind/src/...").
# These don't leak source code but they do reveal the build environment.
# We find ASCII strings in the ELF file region that match known build paths
# and overwrite them with zero bytes. ELF layout is unaffected.
zero_rodata_paths() {
    local path="$1"
    python3 - "${path}" <<'PY'
import re, sys

PATH = sys.argv[1]
with open(PATH, "rb") as f:
    data = bytearray(f.read())

PATTERNS = [
    rb'/rustc/[0-9a-f]{40}/library/[^\x00\n]+',
    rb'/rustc/[0-9a-f]{40}/[^\x00\n]+',
    rb'/home/n/[^\x00\n]+',
    rb'/home/[^\x00\n]+\.cargo/registry/[^\x00\n]+',
    rb'/home/[^\x00\n]+\.rustup/[^\x00\n]+',
    rb'\.cargo/registry/src/[^\x00\n]+',
    rb'\.rustup/toolchains/[^\x00\n]+',
]
combined = re.compile(b'|'.join(b'(?:' + p + b')' for p in PATTERNS))

zeroed = 0
for m in combined.finditer(bytes(data)):
    s, e = m.start(), m.end()
    data[s:e] = b'\x00' * (e - s)
    zeroed += 1

if zeroed:
    with open(PATH, "wb") as f:
        f.write(data)
print(f"      zeroed {zeroed} build-path strings")
PY
}

harden_one() {
    local path="$1"
    local kind="$2"
    local orig_size cur_size removed=0

    if [[ ! -f "${path}" ]]; then
        echo "SKIP ${path} (not present)"
        return 0
    fi

    orig_size=$(stat -c '%s' "${path}")

    echo "==> harden ${path} (${kind}, ${orig_size} bytes)"

    # Nuke build-environment notes and rustc/ld .comment strings.
    for s in "${NUKE_SECTIONS[@]}"; do
        if readelf -S "${path}" 2>/dev/null | grep -q " ${s} "; then
            objcopy --remove-section="${s}" "${path}" 2>/dev/null || true
            removed=$((removed + 1))
        fi
    done

    # Zero /rustc/... and /home/n/... paths baked into .rodata by
    # rustc panic-location tables and our internal build dir.
    zero_rodata_paths "${path}"

    # Re-inject a single, deterministic .comment line (MIND attribution).
    local tmp
    tmp=$(mktemp)
    printf "%s\0" "${COMMENT_LINE}" > "${tmp}"
    objcopy --add-section .comment="${tmp}" \
            --set-section-flags .comment=contents,readonly \
            "${path}" 2>/dev/null || true
    rm -f "${tmp}"

    # Hard strip anything objcopy still considers "unneeded".
    strip --strip-unneeded "${path}" 2>/dev/null || true

    # Runtime-only: also enforce version-script-style export locking
    # by stripping every dynamic symbol that isn't mind_main.
    if [[ "${kind}" == "runtime" ]]; then
        local keep=mind_main
        local dsyms
        dsyms=$(nm -D --defined-only "${path}" 2>/dev/null \
                  | awk '{print $3}' | grep -v "^$" || true)
        for s in ${dsyms}; do
            if [[ "${s}" != "${keep}" ]]; then
                objcopy --strip-symbol="${s}" "${path}" 2>/dev/null || true
            fi
        done
    fi

    # Last pass: zero any residual Build ID description bytes in place.
    # Must run AFTER strip / objcopy / symbol rewrites, because those ops
    # can re-emit the note from internal ELF state. `objcopy --remove-section
    # .note.gnu.build-id` sometimes fails silently on shared objects whose
    # note is covered by a PT_NOTE program header — the section header
    # vanishes but the loaded image keeps the 20-byte fingerprint. Zeroing
    # the description makes the note constant-zero across builds.
    python3 - "${path}" <<'PY'
import subprocess, sys
PATH = sys.argv[1]
try:
    out = subprocess.check_output(["readelf", "-SW", PATH], text=True,
                                  stderr=subprocess.DEVNULL)
except Exception:
    sys.exit(0)

offset = size = 0
import re
for line in out.splitlines():
    if ".note.gnu.bu" in line:
        # "[ 3] .note.gnu.build-id NOTE <addr> <offset> <size> ..."
        m = re.search(r'NOTE\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)', line)
        if m:
            offset = int(m.group(2), 16)
            size   = int(m.group(3), 16)
        break

if size < 36:
    sys.exit(0)

with open(PATH, "rb") as f:
    data = bytearray(f.read())
data[offset + 16 : offset + size] = b'\x00' * (size - 16)
with open(PATH, "wb") as f:
    f.write(data)
PY

    cur_size=$(stat -c '%s' "${path}")
    echo "   sections removed: ${removed}  size: ${orig_size} -> ${cur_size}"
}

# ---------------------------------------------------------------------------
harden_one "${BIN}/libxrmgov"                     "mind-elf"
harden_one "${BIN}/libmind_cpu_linux-x64.so"      "runtime"
harden_one "${BIN}/xrm_mind_port"                 "rust-bin"

# ---------------------------------------------------------------------------
echo
echo "==> Verification: no residual build-environment strings"
LEAKS=0
for f in "${BIN}/libxrmgov" "${BIN}/libmind_cpu_linux-x64.so" "${BIN}/xrm_mind_port"; do
    # Rust/LLD/GCC leaks
    n=$(strings "${f}" 2>/dev/null \
          | grep -cE 'rustc version|LLD [0-9]+|Ubuntu [0-9]+\.[0-9]+\.[0-9]+|GCC: \(|/home/|\.cargo/registry|/rustc/' \
          || true)
    if [[ "${n}" -gt 0 ]]; then
        echo "   [LEAK ${n}] ${f}"
        strings "${f}" 2>/dev/null \
          | grep -E 'rustc version|LLD [0-9]+|Ubuntu [0-9]+\.[0-9]+\.[0-9]+|GCC: \(|/home/|\.cargo/registry|/rustc/' \
          | head -3 | sed 's/^/      /'
        LEAKS=$((LEAKS + n))
    fi
done

if [[ "${LEAKS}" -gt 0 ]]; then
    echo
    echo "   FAIL: ${LEAKS} residual build-env strings."
    exit 2
fi
echo "   [OK] no residual rustc / LLD / GCC / path strings in any shipped binary"

# ---------------------------------------------------------------------------
echo
echo "==> Verification: exports are minimal"
echo "   libxrmgov:"
nm -D --defined-only "${BIN}/libxrmgov" 2>&1 | sed 's/^/      /' | head -10 \
    || echo "      (no dynamic symbols)"
echo "   libmind_cpu_linux-x64.so:"
nm -D --defined-only "${BIN}/libmind_cpu_linux-x64.so" 2>&1 | sed 's/^/      /' | head -10

# ---------------------------------------------------------------------------
echo
echo "==> Verification: .comment is MIND attribution only"
for f in "${BIN}/libxrmgov" "${BIN}/libmind_cpu_linux-x64.so" "${BIN}/xrm_mind_port"; do
    c=$(objcopy --dump-section .comment=/dev/stdout "${f}" 2>/dev/null | tr -d '\0' || true)
    printf "   %-40s %s\n" "$(basename "${f}")" "${c}"
done

# ---------------------------------------------------------------------------
echo
echo "==> Regenerate SHA256SUMS"
(cd "${BIN}" && sha256sum libxrmgov libmind_cpu_linux-x64.so xrm_mind_port > SHA256SUMS)
cat "${BIN}/SHA256SUMS"
echo
echo "Hardening complete."
