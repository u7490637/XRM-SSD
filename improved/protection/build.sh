#!/bin/bash
# xrm-ssd Protected Governance Library — Build Script
# Copyright (c) 2026 STARGA, Inc. All rights reserved.
# PROPRIETARY AND CONFIDENTIAL
#
# Usage:
#   protection/build.sh             # Build and deploy to improved/bin/
#   protection/build.sh --strip     # Build stripped
#   protection/build.sh --deploy /path
#
# Build process (5 stages):
#   Stage 1: mindc compiles all .mind sources with [protection] transforms
#            (obfuscate_strings, anti_debug, anti_tamper, vm_protection)
#   Stage 2: Strip source embedding from MIND objects
#   Stage 3: C ABI wrapper compiled and linked with MIND objects
#            (provides xrmgov_* symbols for Rust/ctypes FFI)
#   Stage 4: Verify exports and protection
#   Stage 5: Strip source data and symbols from final binary
#
# IMPORTANT: protection.c compiled to c_protection.o (NOT protection.o)
# to avoid overwriting mindc's protection.mind -> protection.o output.
# The .mind protection IS the real protection. The C files are the ABI bridge.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IMPROVED_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEPLOY_DIR="${DEPLOY_DIR:-${IMPROVED_DIR}/bin}"
MIND_LIB_DIR="${MIND_LIB_DIR:-/home/n/.nikolachess/lib}"
MINDC="${MINDC:-mindc}"

# Verify mindc
if ! command -v "${MINDC}" &>/dev/null; then
    echo "ERROR: mindc not found. Locked protection requires MIND compiler."
    exit 1
fi

STRIP_FLAG=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --strip) STRIP_FLAG="-s"; shift ;;
        --deploy) DEPLOY_DIR="$2"; shift 2 ;;
        *) shift ;;
    esac
done

# Detect platform
case "$(uname -s)" in
    Linux*)
        EXT="so"
        PLATFORM_FLAGS="-lpthread -ldl"
        MIND_RT="libmind_cpu_linux-x64.so"
        ;;
    Darwin*)
        EXT="dylib"
        PLATFORM_FLAGS="-lpthread -ldl"
        MIND_RT="libmind_cpu_macos-x64.dylib"
        ;;
    *)
        echo "ERROR: Unsupported platform: $(uname -s)"
        exit 1
        ;;
esac

OUTPUT="libxrmgov.${EXT}"
OBJ_DIR="${IMPROVED_DIR}/target/obj"

echo "=== xrm-ssd Protected Governance Library Build ==="
echo "Platform:  $(uname -s) $(uname -m)"
echo "Compiler:  $(${MINDC} --version 2>/dev/null || echo 'not found')"
echo "Runtime:   ${MIND_LIB_DIR}/${MIND_RT}"
echo "Output:    ${OUTPUT}"
echo "Deploy to: ${DEPLOY_DIR}"
echo "Mode:      Locked protection (MIND + C ABI bridge)"
echo ""

# Verify MIND runtime exists
if [ ! -f "${MIND_LIB_DIR}/${MIND_RT}" ]; then
    echo "ERROR: MIND runtime not found at ${MIND_LIB_DIR}/${MIND_RT}"
    exit 1
fi

# ── Stage 1: MIND compilation ──────────────────────────────────────────────
echo "[1/5] Compiling MIND sources (mindc build --release)..."
echo "  Mind.toml [protection]: obfuscate_strings + anti_debug + anti_tamper + vm_protection"
cd "${IMPROVED_DIR}"

rm -rf "${OBJ_DIR}"

MIND_LIB_PATH="${MIND_LIB_DIR}" ${MINDC} build --release --target cpu -v 2>&1 | sed 's/^/  /'

MIND_OBJ_COUNT=$(find "${OBJ_DIR}" -name "*.o" -type f 2>/dev/null | wc -l)
echo "  MIND objects: ${MIND_OBJ_COUNT} files"
echo ""

# ── Stage 2: Strip source embedding from MIND objects ─────────────────────
echo "[2/5] Stripping source embedding from MIND objects..."

STRIPPED=0
for obj_file in "${OBJ_DIR}"/*.o; do
    [ -f "${obj_file}" ] || continue
    SOURCE_SYMS=$(nm "${obj_file}" 2>/dev/null | grep "mind_module_.*_get_source" | awk '{print $3}' || true)
    IR_SYMS=$(nm "${obj_file}" 2>/dev/null | grep "mind_module_.*_get_ir" | awk '{print $3}' || true)

    ALL_STRIP_SYMS="${SOURCE_SYMS} ${IR_SYMS}"
    ALL_STRIP_SYMS=$(echo "${ALL_STRIP_SYMS}" | xargs)

    if [ -n "${ALL_STRIP_SYMS}" ]; then
        STRIP_ARGS=""
        for sym in ${ALL_STRIP_SYMS}; do
            STRIP_ARGS="${STRIP_ARGS} --strip-symbol=${sym}"
        done
        objcopy ${STRIP_ARGS} "${obj_file}"
        STRIPPED=$((STRIPPED + 1))
    fi
done

# Zero embedded source strings (.rodata) in MIND objects
for obj_file in "${OBJ_DIR}"/*.o; do
    [ -f "${obj_file}" ] || continue
    case "$(basename "${obj_file}")" in c_*) continue ;; esac
    python3 -c "
import sys
with open('${obj_file}', 'rb') as f:
    data = bytearray(f.read())

KEYWORDS = [
    b'fn ', b'pub fn', b'@export', b'static var', b'const ',
    b'import ', b'// ', b'.mind', b'-> ', b'protection',
    b'var ', b'let ', b'return ', b'for ', b'while ',
    b'mem.copy', b'mem.zero', b'mem.alloc', b'0..', b' in 0',
    b'reduce_sum', b'sum_all', b'min_all', b'max_all',
    b'#[cfg(', b'#[extern(', b'#[export', b'#[inline',
    b'canary', b'watchdog', b'corrupt', b'ptrace',
    b'debugger', b'siphash', b'heartbeat', b'verify_all',
    b'check_integrity', b'verify_state', b'run_vm_',
    b'setup_early', b'check_debugger', b'check_hooks', b'check_environment',
    b'TracerPid', b'LD_PRELOAD', b'DYLD_INSERT',
    b'PR_SET_DUMPABLE', b'PTRACE_TRACEME', b'PTRACE_DETACH',
    b'IsDebuggerPresent', b'CheckRemoteDebugger',
    b'0x4D,', b'0x31,', b'0x4E,', b'0x44,',
    b'0x58,', b'0x52,', b'0x4D,', b'0x47,', b'0x4F,',
    b'0x56,', b'AUTH_KEY', b'STRING_KEY',
    b'mic@', b'T0 i32', b'N0 const', b'N1 const', b'N2 const',
    b'VM_SYS_', b'VM_OP_',
    b'xrm-ssd Protection', b'protection.mind',
    b'corrupt_and_die',
    b'gov9', b'inv1_', b'inv2_', b'inv3_', b'inv4_', b'inv5_',
    b'inv6_', b'inv7_', b'inv8_', b'inv9_',
]

i = 0
zeroed = 0
while i < len(data) - 4:
    if 32 <= data[i] <= 126:
        start = i
        while i < len(data) and (32 <= data[i] <= 126 or data[i] in (9, 10, 13)):
            i += 1
        length = i - start
        if length >= 6:
            segment = bytes(data[start:start+length])
            if any(kw in segment for kw in KEYWORDS):
                data[start:start+length] = b'\x00' * length
                zeroed += 1
    else:
        i += 1

with open('${obj_file}', 'wb') as f:
    f.write(data)
" 2>/dev/null || true
done
echo "  Objects stripped: ${STRIPPED}/${MIND_OBJ_COUNT}"
echo ""

# ── Stage 3: C ABI bridge + link ──────────────────────────────────────────
echo "[3/5] Compiling C ABI bridge and linking..."

MIND_RT_LINK="${MIND_RT%.so}"
MIND_RT_LINK="${MIND_RT_LINK%.dylib}"
MIND_RT_LINK="${MIND_RT_LINK#lib}"

# Compile C bridge files into the mindc obj directory, prefixed c_*
for c_src in "xrmgov_protected.c" "protection.c"; do
    c_obj="${OBJ_DIR}/c_$(basename "${c_src}" .c).o"
    echo "  Compiling: ${c_src} -> $(basename ${c_obj})"
    cc -c -fPIC -fvisibility=hidden -O3 \
        -Wall -Wextra -Wno-unused-parameter \
        -I"${SCRIPT_DIR}/src" \
        -o "${c_obj}" \
        "${SCRIPT_DIR}/src/${c_src}"
done
echo ""

echo "  Linking shared library..."

ALL_OBJS=$(find "${OBJ_DIR}" -name "*.o" -type f 2>/dev/null | tr '\n' ' ')
OBJ_COUNT=$(echo ${ALL_OBJS} | wc -w | tr -d ' ')
echo "  Objects: ${OBJ_COUNT} (${MIND_OBJ_COUNT} MIND + 2 C ABI)"

VERSION_SCRIPT="${OBJ_DIR}/exports.map"
cat > "${VERSION_SCRIPT}" <<'VERSCRIPT'
{
    global:
        xrmgov_inv1_non_negative;
        xrmgov_inv2_sum_bounded;
        xrmgov_inv3_l2_bounded;
        xrmgov_inv4_mean_in_band;
        xrmgov_inv5_variance_bounded;
        xrmgov_inv6_max_bounded;
        xrmgov_inv7_row_sums_nonneg;
        xrmgov_inv8_col_range_bounded;
        xrmgov_inv9_determinism_fence;
        xrmgov_gov9_evaluate;
        xrmgov_init;
        xrmgov_check;
        xrmgov_protected;
        xrmgov_verified;
        xrmgov_version;
        xrmgov_shutdown;
    local:
        *;
};
VERSCRIPT
echo "  Version script: 16 global exports"

MIND_LINK_FLAGS="-L${MIND_LIB_DIR} -l${MIND_RT_LINK} -Wl,-rpath,\$ORIGIN -Wl,-rpath,${MIND_LIB_DIR}"

cc -shared -fPIC \
    -fvisibility=hidden \
    -O3 \
    ${STRIP_FLAG} \
    -Wl,--version-script="${VERSION_SCRIPT}" \
    -Wl,--gc-sections \
    -o "${SCRIPT_DIR}/${OUTPUT}" \
    ${ALL_OBJS} \
    -lm ${PLATFORM_FLAGS} ${MIND_LINK_FLAGS}

# Rewrite .comment section
objcopy --remove-section .comment "${SCRIPT_DIR}/${OUTPUT}" 2>/dev/null || true
MINDC_VER=$(${MINDC} --version 2>/dev/null | head -1 || echo "mind 0.2.3")
printf "MIND: ${MINDC_VER} (STARGA toolchain)\0" > /tmp/.mind_comment
objcopy --add-section .comment=/tmp/.mind_comment --set-section-flags .comment=contents,readonly "${SCRIPT_DIR}/${OUTPUT}" 2>/dev/null || true
rm -f /tmp/.mind_comment

echo "  Built: ${SCRIPT_DIR}/${OUTPUT}"
echo "  Size:  $(du -h "${SCRIPT_DIR}/${OUTPUT}" | cut -f1)"
echo ""

# ── Stage 4: Verify exports and protection ────────────────────────────────
echo "[4/5] Verifying exports and protection..."
EXPECTED_EXPORTS="xrmgov_inv1_non_negative xrmgov_inv2_sum_bounded xrmgov_inv3_l2_bounded xrmgov_inv4_mean_in_band xrmgov_inv5_variance_bounded xrmgov_inv6_max_bounded xrmgov_inv7_row_sums_nonneg xrmgov_inv8_col_range_bounded xrmgov_inv9_determinism_fence xrmgov_gov9_evaluate xrmgov_init xrmgov_check xrmgov_protected xrmgov_verified xrmgov_version xrmgov_shutdown"

MISSING=0
for sym in ${EXPECTED_EXPORTS}; do
    if nm -D "${SCRIPT_DIR}/${OUTPUT}" 2>/dev/null | grep -q " T ${sym}$"; then
        echo "  [OK] ${sym}"
    else
        echo "  [MISSING] ${sym}"
        MISSING=$((MISSING + 1))
    fi
done

if [ "${MISSING}" -gt 0 ]; then
    echo "ERROR: ${MISSING} expected symbols missing!"
    exit 1
fi

# Protection internals should be hidden
echo ""
echo "  Checking protection internals are hidden..."
HIDDEN_CHECK=0
for sym in corrupt_and_die is_debugger_present_native watchdog_loop vm_execute check_hooks check_environment check_path xrmgov_protection_init xrmgov_heartbeat xrmgov_auth_challenge xrmgov_auth_verify xrmgov_auth_is_verified xrmgov_is_protected xrmgov_get_version xrmgov_shutdown_protection; do
    if nm -D "${SCRIPT_DIR}/${OUTPUT}" 2>/dev/null | grep -q " T ${sym}$"; then
        echo "  [LEAKED] ${sym} — should be hidden!"
        HIDDEN_CHECK=$((HIDDEN_CHECK + 1))
    fi
done
if [ "${HIDDEN_CHECK}" -eq 0 ]; then
    echo "  [OK] All protection internals hidden"
fi

# Check MIND runtime link
if nm -D "${SCRIPT_DIR}/${OUTPUT}" 2>/dev/null | grep -q "mind_runtime"; then
    echo "  [OK] MIND runtime linked"
fi

# Check no source symbol leak
echo ""
echo "  Checking for source leaks..."
SOURCE_LEAK=0
if nm -D "${SCRIPT_DIR}/${OUTPUT}" 2>/dev/null | grep -q "get_source"; then
    echo "  [LEAKED] get_source symbols still present!"
    SOURCE_LEAK=1
fi
if strings "${SCRIPT_DIR}/${OUTPUT}" 2>/dev/null | grep -qE "^(fn |pub fn |@export|static var |const .*: [iu])" ; then
    echo "  [LEAKED] Plaintext .mind source found in binary!"
    SOURCE_LEAK=1
fi
if [ "${SOURCE_LEAK}" -eq 0 ]; then
    echo "  [OK] No source code embedded in binary"
fi

# ── Stage 5: Strip final binary ───────────────────────────────────────────
echo ""
echo "[5/5] Stripping source data and symbols from final binary..."

python3 -c "
import subprocess

with open('${SCRIPT_DIR}/${OUTPUT}', 'rb') as f:
    data = bytearray(f.read())

rodata_start = 0
rodata_end = len(data)
try:
    out = subprocess.check_output(
        ['readelf', '-S', '${SCRIPT_DIR}/${OUTPUT}'],
        text=True, stderr=subprocess.DEVNULL
    )
    for line in out.splitlines():
        if '.rodata' in line:
            parts = line.split()
            for i, p in enumerate(parts):
                if p == '.rodata':
                    off_idx = i + 3
                    rodata_start = int(parts[off_idx], 16)
                    rodata_end = rodata_start + int(parts[off_idx + 1], 16)
                    break
            break
except Exception:
    pass

KEYWORDS = [
    b'fn ', b'pub fn', b'@export', b'static var', b'const ',
    b'import ', b'// ', b'.mind', b'-> bool', b'-> i32', b'-> i64',
    b'-> u8', b'-> u32', b'-> u64', b'-> *void', b': i32', b': i64',
    b': u8', b': u32', b': u64', b': bool', b': *void',
    b'var ', b'let ', b'return ', b'for ', b'while ',
    b'mem.copy', b'mem.zero', b'mem.alloc', b'0..', b' in 0',
    b'reduce_sum', b'sum_all',
    b'#[cfg(', b'#[extern(', b'#[export', b'#[inline',
    b'canary', b'watchdog', b'corrupt_and_die', b'corrupt',
    b'ptrace', b'debugger', b'siphash', b'heartbeat', b'verify_all',
    b'check_integrity', b'verify_state', b'run_vm_',
    b'setup_early', b'check_debugger', b'check_hooks', b'check_environment',
    b'TracerPid', b'LD_PRELOAD', b'DYLD_INSERT',
    b'PR_SET_DUMPABLE', b'PTRACE_TRACEME', b'PTRACE_DETACH',
    b'IsDebuggerPresent', b'CheckRemoteDebugger',
    b'xrm-ssd Protection', b'protection.mind',
    b'0x4D,', b'0x58,', b'0x52,', b'0x47,',
    b'AUTH_KEY', b'STRING_KEY',
    b'mic@', b'T0 i32', b'N0 const', b'N1 const', b'N2 const',
    b'VM_SYS_', b'VM_OP_',
    b'gov9', b'inv1_', b'inv2_', b'inv3_', b'inv4_', b'inv5_',
    b'inv6_', b'inv7_', b'inv8_', b'inv9_',
]

zeroed = 0
i = rodata_start
scan_end = min(rodata_end, len(data) - 4) if rodata_end > rodata_start else len(data) - 4
while i < scan_end:
    if 32 <= data[i] <= 126:
        start = i
        while i < scan_end and (32 <= data[i] <= 126 or data[i] in (9, 10, 13)):
            i += 1
        length = i - start
        if length >= 6:
            segment = bytes(data[start:start+length])
            if any(kw in segment for kw in KEYWORDS):
                data[start:start+length] = b'\x00' * length
                zeroed += 1
    else:
        i += 1

with open('${SCRIPT_DIR}/${OUTPUT}', 'wb') as f:
    f.write(data)
print(f'  Zeroed {zeroed} source strings in final binary')
"

if command -v patchelf &>/dev/null; then
    echo "  Patching RPATH (removing absolute build paths)..."
    patchelf --set-rpath '$ORIGIN' "${SCRIPT_DIR}/${OUTPUT}"
    echo "  [OK] RPATH set to \$ORIGIN only"
else
    echo "  WARNING: patchelf not found"
fi

BEFORE_SIZE=$(stat -c%s "${SCRIPT_DIR}/${OUTPUT}")
strip -s "${SCRIPT_DIR}/${OUTPUT}"
AFTER_SIZE=$(stat -c%s "${SCRIPT_DIR}/${OUTPUT}")
echo "  Before strip: ${BEFORE_SIZE} bytes"
echo "  After strip:  ${AFTER_SIZE} bytes"

# Final leak verification (8 categories)
echo ""
echo "  Comprehensive leak verification..."
FINAL_LEAK=0

C1=$(strings "${SCRIPT_DIR}/${OUTPUT}" 2>/dev/null | grep -cE "fn |pub fn|static var|@export|const .*: [iu]|var [a-z]|let [a-z]|return |mem\.(copy|zero|alloc)|for .* in 0\.\." || true)
[ "${C1}" -gt 0 ] && { echo "  [LEAKED] ${C1} .mind source patterns"; FINAL_LEAK=$((FINAL_LEAK + C1)); }

C2=$(strings "${SCRIPT_DIR}/${OUTPUT}" 2>/dev/null | grep -cE '#\[cfg\(|#\[extern\(|#\[export|#\[inline' || true)
[ "${C2}" -gt 0 ] && { echo "  [LEAKED] ${C2} MIND attribute patterns"; FINAL_LEAK=$((FINAL_LEAK + C2)); }

C4=$(strings "${SCRIPT_DIR}/${OUTPUT}" 2>/dev/null | grep -cE '0x4[0-9A-F], 0x3[0-9A-F]|0x58, 0x52|AUTH_KEY|STRING_KEY' || true)
[ "${C4}" -gt 0 ] && { echo "  [LEAKED] ${C4} auth key patterns"; FINAL_LEAK=$((FINAL_LEAK + C4)); }

C5=$(strings "${SCRIPT_DIR}/${OUTPUT}" 2>/dev/null | grep -cE '/home/n|\.nikolachess|/rustc/|\.cargo/registry' || true)
[ "${C5}" -gt 0 ] && { echo "  [LEAKED] ${C5} build path patterns"; FINAL_LEAK=$((FINAL_LEAK + C5)); }

C6=$(strings "${SCRIPT_DIR}/${OUTPUT}" 2>/dev/null | grep -cE 'mic@|T[0-9] i32|N[0-9] const|VM_SYS_|VM_OP_' || true)
[ "${C6}" -gt 0 ] && { echo "  [LEAKED] ${C6} VM IR patterns"; FINAL_LEAK=$((FINAL_LEAK + C6)); }

C7=$(strings "${SCRIPT_DIR}/${OUTPUT}" 2>/dev/null | grep -cE 'canary|watchdog|corrupt_and_die|siphash|heartbeat|verify_all|setup_early|check_integrity|verify_state|run_vm_' || true)
[ "${C7}" -gt 0 ] && { echo "  [LEAKED] ${C7} protection internal patterns"; FINAL_LEAK=$((FINAL_LEAK + C7)); }

C8=$(strings "${SCRIPT_DIR}/${OUTPUT}" 2>/dev/null | grep -cE 'inv[0-9]_|gov9_evaluate|_non_negative|_sum_bounded|_l2_bounded|_mean_in_band|_variance_bounded|_max_bounded|_row_sums|_col_range|_determinism' || true)
# xrmgov_* exported names are expected; subtract 16 (exact export count)
if [ "${C8}" -gt 16 ]; then
    EXCESS=$((C8 - 16))
    echo "  [LEAKED] ${EXCESS} extra gov9/invariant patterns (beyond 16 exports)"
    FINAL_LEAK=$((FINAL_LEAK + EXCESS))
fi

if [ "${FINAL_LEAK}" -gt 0 ]; then
    echo ""
    echo "  FAIL: ${FINAL_LEAK} total leaked patterns."
    exit 1
else
    echo "  [OK] Zero leaked patterns across all categories"
fi

# ── Deploy ────────────────────────────────────────────────────────────────
echo ""
echo "Deploying..."
mkdir -p "${DEPLOY_DIR}"
cp "${SCRIPT_DIR}/${OUTPUT}" "${DEPLOY_DIR}/${OUTPUT}"

# Deploy MIND runtime alongside
cp "${MIND_LIB_DIR}/${MIND_RT}" "${DEPLOY_DIR}/${MIND_RT}"
objcopy --remove-section .comment "${DEPLOY_DIR}/${MIND_RT}" 2>/dev/null || true
printf "MIND: mind-runtime (STARGA toolchain)\0" > /tmp/.mind_rt_comment
objcopy --add-section .comment=/tmp/.mind_rt_comment --set-section-flags .comment=contents,readonly "${DEPLOY_DIR}/${MIND_RT}" 2>/dev/null || true
rm -f /tmp/.mind_rt_comment

LIB_SIZE=$(du -h "${DEPLOY_DIR}/${OUTPUT}" | cut -f1)
RT_SIZE=$(du -h "${DEPLOY_DIR}/${MIND_RT}" | cut -f1)
echo "  Library: ${DEPLOY_DIR}/${OUTPUT} (${LIB_SIZE})"
echo "  Runtime: ${DEPLOY_DIR}/${MIND_RT} (${RT_SIZE})"

echo ""
echo "=== Build complete (locked protection) ==="
