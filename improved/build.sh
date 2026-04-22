#!/usr/bin/env bash
# XRM-SSD + MIND hybrid - protected build script
# Copyright (c) 2026 STARGA, Inc. All rights reserved.
#
# Produces two binaries in ./bin/ :
#
#   libxrmgov        mindc-compiled MIND executable. Links src/lib.mind +
#                    src/gov9.mind. Source-embedding symbols stripped,
#                    .rodata source strings zeroed, RPATH normalized,
#                    .comment carries the MIND toolchain attribution.
#
#   xrm_mind_port    Rust bench harness. Reimplements the nine gov9
#                    invariants line-for-line (PORTED_FROM: comments in
#                    src/main.rs) and measures TPS at 1/5/10/25/50 %
#                    intervention ratios. Emits an SHA-256 evidence chain.
#
# Both binaries are LTO-built, stripped, and SHA-256 hashed in bin/SHA256SUMS.

set -euo pipefail
cd "$(dirname "$0")"

MINDC="${MINDC:-mindc}"
CARGO="${CARGO:-cargo}"
MIND_LIB_DIR="${MIND_LIB_DIR:-/home/n/.nikolachess/lib}"

# cargo PATH fallback
if ! command -v "${CARGO}" >/dev/null 2>&1; then
    for p in "${HOME}/.cargo/bin/cargo" "/usr/local/cargo/bin/cargo"; do
        if [[ -x "${p}" ]]; then
            CARGO="${p}"; export PATH="$(dirname "${p}"):${PATH}"; break
        fi
    done
fi

OUT="bin"
mkdir -p "${OUT}"

echo "==> [1/6] mindc compile src/lib.mind + src/gov9.mind"
rm -rf target/obj target/release 2>/dev/null || true
MIND_LIB_PATH="${MIND_LIB_DIR}" "${MINDC}" build --release --target cpu 2>&1 | sed 's/^/   /'

MIND_ELF=$(find target/release -maxdepth 1 -type f -executable ! -name "*.o" ! -name "*.so" | head -1 || true)
if [[ -z "${MIND_ELF}" ]]; then
    echo "FATAL: mindc did not produce any executable in target/release/" >&2
    ls -la target/release/ >&2 || true
    exit 2
fi
echo "   mindc emitted: ${MIND_ELF}"
cp "${MIND_ELF}" "${OUT}/libxrmgov"

# ---------------------------------------------------------------------------
echo "==> [2/6] Strip source-embedding symbols (get_source / get_ir / SOURCE / IR)"
SYMS=$(nm "${OUT}/libxrmgov" 2>/dev/null | \
    grep -E "(mind_module_.*_(get_source|get_ir)|MIND_(MODULE_.*_)?(SOURCE|IR)(_lib)?)" | \
    awk '{print $NF}' | sort -u || true)
if [[ -n "${SYMS}" ]]; then
    STRIP_ARGS=()
    for s in ${SYMS}; do STRIP_ARGS+=(--strip-symbol="${s}"); done
    objcopy "${STRIP_ARGS[@]}" "${OUT}/libxrmgov"
    echo "   stripped $(echo "${SYMS}" | wc -w | tr -d ' ') source-embedding symbols"
fi

# ---------------------------------------------------------------------------
echo "==> [3/6] Zero source / path / IR strings in .rodata"
python3 <<'PY'
import re, struct, subprocess

PATH = "bin/libxrmgov"
with open(PATH, "rb") as f:
    data = bytearray(f.read())

# Locate .rodata for safer scoping.
ro_start, ro_end = 0, len(data)
try:
    out = subprocess.check_output(["readelf", "-S", PATH], text=True,
                                  stderr=subprocess.DEVNULL)
    for line in out.splitlines():
        if ".rodata" in line:
            parts = line.split()
            for i, p in enumerate(parts):
                if p == ".rodata":
                    ro_start = int(parts[i + 3], 16)
                    ro_end = ro_start + int(parts[i + 4], 16)
                    break
            break
except Exception:
    pass

KEYWORDS = [
    # MIND source patterns
    b"fn ", b"pub fn", b"@export", b"static var", b"const ",
    b"import std", b"// XRM", b"// Copyright", b".mind",
    b"-> tensor<", b"-> bool", b"-> i32", b"-> f32",
    b"var ", b"let ", b"return ", b"for ", b"while ",
    b"tensor.zeros", b"tensor<f32", b"sum_all", b"min_all", b"max_all",
    b"select(", b"sqrt(", b"abs(", b"mean_all",
    # Comments and bench docstrings
    b"PORTED_FROM:", b"gov9 invariants", b"governance kernel",
    b"governance tick", b"intervention ratio", b"determinism fence",
    b"all-pass bitmask",
    # Build paths
    b"/home/", b".nikolachess", b".cargo/registry",
    # Internal IR / MIC patterns
    b"mic@", b"VM_OP_", b"VM_SYS_", b"MIND_BACKEND",
    b"MIND_IR_", b"MIND_SOURCE_", b"MIND_MODULE_",
    # mindc-injected helper text
    b"mindlang.dev/enterprise",
]

def looks_text(b):
    return 32 <= b <= 126 or b in (9, 10, 13)

zeroed = 0
i = ro_start
end = min(ro_end, len(data) - 1)
while i < end:
    if 32 <= data[i] <= 126:
        s = i
        while i < end and looks_text(data[i]):
            i += 1
        if i - s >= 6:
            seg = bytes(data[s:i])
            if any(k in seg for k in KEYWORDS):
                data[s:i] = b"\x00" * (i - s)
                zeroed += 1
    else:
        i += 1

with open(PATH, "wb") as f:
    f.write(data)
print(f"   zeroed {zeroed} source/path strings in .rodata "
      f"(0x{ro_start:x}..0x{ro_end:x})")
PY

# ---------------------------------------------------------------------------
echo "==> [4/6] Normalize RPATH and replace .comment with MIND attribution"
if command -v patchelf >/dev/null 2>&1; then
    patchelf --set-rpath '$ORIGIN' "${OUT}/libxrmgov"
    echo "   RPATH set to \$ORIGIN"
else
    echo "   patchelf not installed — RPATH may carry build paths"
fi

objcopy --remove-section .comment "${OUT}/libxrmgov" 2>/dev/null || true
MINDC_VER=$("${MINDC}" --version 2>/dev/null | head -1 || echo "mind 0.2.3")
TMP_COMMENT=$(mktemp)
printf "MIND: %s (STARGA toolchain)\0" "${MINDC_VER}" > "${TMP_COMMENT}"
objcopy --add-section .comment="${TMP_COMMENT}" \
        --set-section-flags .comment=contents,readonly \
        "${OUT}/libxrmgov" 2>/dev/null || true
rm -f "${TMP_COMMENT}"

strip --strip-unneeded "${OUT}/libxrmgov" 2>/dev/null || true

# runtime bundle added:
# Bundle libmind_cpu_linux-x64.so alongside libxrmgov so the protected
# binary runs without LD_LIBRARY_PATH on Polo's L4 (RPATH=$ORIGIN).
RUNTIME_SRC="${MIND_LIB_DIR}/libmind_cpu_linux-x64.so"
if [[ -f "${RUNTIME_SRC}" ]]; then
    cp "${RUNTIME_SRC}" "${OUT}/libmind_cpu_linux-x64.so"
    strip "${OUT}/libmind_cpu_linux-x64.so" 2>/dev/null || true
    objcopy --remove-section .comment "${OUT}/libmind_cpu_linux-x64.so" 2>/dev/null || true
    echo "   bundled libmind_cpu_linux-x64.so ($(du -h ${OUT}/libmind_cpu_linux-x64.so | cut -f1))"
else
    echo "   WARN: ${RUNTIME_SRC} missing — libxrmgov will require LD_LIBRARY_PATH"
fi


# ---------------------------------------------------------------------------
echo "==> [5/6] cargo build --release (Rust harness)"
# Separate target dir so mindc's target/release wipe doesn't corrupt cargo cache.
"${CARGO}" build --release --target-dir target-rust 2>&1 | sed 's/^/   /'
cp target-rust/release/xrm_mind_port "${OUT}/xrm_mind_port"
strip "${OUT}/xrm_mind_port" 2>/dev/null || true

# ---------------------------------------------------------------------------
echo "==> [6/6] Leak verification + SHA-256"
LEAKS=0
check() {
    local label="$1" pattern="$2"
    local n
    n=$(strings "${OUT}/libxrmgov" 2>/dev/null | grep -cE "${pattern}" || true)
    if [[ "${n}" -gt 0 ]]; then
        echo "   [LEAK ${n}] ${label}"
        strings "${OUT}/libxrmgov" 2>/dev/null | grep -E "${pattern}" | head -3 | sed 's/^/      /'
        LEAKS=$((LEAKS + n))
    fi
}
check "MIND source syntax"  'fn (inv|gov9|main)|tensor<f32|tensor\.zeros|sum_all|min_all|max_all|select\('
check "Comment markers"     '// (XRM|Copyright|gov9|all-pass|PORTED)'
check "Build paths"         '/home/|\.nikolachess|\.cargo/registry'
check "MIND module symbols" 'MIND_(MODULE|SOURCE|IR|BACKEND)|mic@|VM_OP_|VM_SYS_'

if [[ "${LEAKS}" -gt 0 ]]; then
    echo
    echo "   FAIL: ${LEAKS} leaked patterns in libxrmgov."
    exit 3
fi
echo "   [OK] libxrmgov: zero source / path / IR leaks"

# Confirm .comment line is MIND-only.
COMMENT=$(objcopy --dump-section .comment=/dev/stdout "${OUT}/libxrmgov" 2>/dev/null \
            | tr -d '\0' || true)
echo "   .comment: ${COMMENT}"

(cd "${OUT}" && sha256sum libmind_cpu_linux-x64.so libxrmgov xrm_mind_port > SHA256SUMS)
echo
echo "Build complete:"
ls -la "${OUT}/"
echo
cat "${OUT}/SHA256SUMS"
echo
echo "Quick smoke test:"
echo "   cd ${OUT} && ./libxrmgov   # runtime bundled; RPATH=\$ORIGIN"
echo "   cd ${OUT} && ./xrm_mind_port --iter 10000"
