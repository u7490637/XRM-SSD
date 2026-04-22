#!/usr/bin/env bash
# XRM-SSD + MIND hybrid - build script
# Copyright (c) 2026 STARGA, Inc. All rights reserved.
#
# Produces two binaries in ./bin/ :
#
#   libxrmgov        mindc-compiled MIND executable. Runs src/lib.mind,
#                    which evaluates gov9 on a fixed synthetic batch and
#                    prints the verdict bitmask. Proof the kernel compiles.
#
#   xrm_mind_port    Rust bench harness. Reimplements the nine gov9
#                    invariants line-for-line (see PORTED_FROM: comments
#                    in src/main.rs) and measures TPS at 1/5/10/25/50 %
#                    intervention ratios. Emits an evidence chain root.
#
# Both binaries include SHA-256 digests written to bin/SHA256SUMS.

set -euo pipefail
cd "$(dirname "$0")"

MINDC="${MINDC:-mindc}"
CARGO="${CARGO:-cargo}"
if ! command -v "${CARGO}" >/dev/null 2>&1; then
    for p in "${HOME}/.cargo/bin/cargo" "/usr/local/cargo/bin/cargo"; do
        if [[ -x "${p}" ]]; then CARGO="${p}"; export PATH="$(dirname "${p}"):${PATH}"; break; fi
    done
fi
OUT="bin"
mkdir -p "${OUT}"

echo "==> [1/4] mindc compile src/lib.mind + src/gov9.mind"
rm -rf target/obj target/release 2>/dev/null || true
"${MINDC}" build --release --target cpu 2>&1 | sed 's/^/   /'

MIND_ELF=$(find target/release -maxdepth 1 -type f -executable ! -name "*.o" ! -name "*.so" | head -1 || true)
if [[ -z "${MIND_ELF}" ]]; then
    echo "FATAL: mindc did not produce any executable in target/release/" >&2
    ls -la target/release/ >&2 || true
    exit 2
fi
echo "   mindc emitted: ${MIND_ELF}"
cp "${MIND_ELF}" "${OUT}/libxrmgov"

echo "==> [2/4] Strip source-embedding symbols from MIND binary"
SOURCE_SYMS=$(nm "${OUT}/libxrmgov" 2>/dev/null | grep -E "mind_module_.*_(get_source|get_ir)" | awk '{print $3}' || true)
if [[ -n "${SOURCE_SYMS}" ]]; then
    STRIP_ARGS=()
    for s in ${SOURCE_SYMS}; do STRIP_ARGS+=(--strip-symbol="${s}"); done
    objcopy "${STRIP_ARGS[@]}" "${OUT}/libxrmgov" 2>/dev/null || true
    echo "   stripped $(echo "${SOURCE_SYMS}" | wc -w | tr -d ' ') source-embedding symbols"
fi
strip --strip-unneeded "${OUT}/libxrmgov" 2>/dev/null || true

echo "==> [3/4] cargo build --release (Rust harness)"
"${CARGO}" build --release 2>&1 | sed 's/^/   /'
cp target/release/xrm_mind_port "${OUT}/xrm_mind_port"
strip "${OUT}/xrm_mind_port" 2>/dev/null || true

echo "==> [4/4] SHA-256 both binaries"
(cd "${OUT}" && sha256sum libxrmgov xrm_mind_port > SHA256SUMS)
cat "${OUT}/SHA256SUMS"

echo
echo "Build complete:"
ls -la "${OUT}/"
echo
echo "Quick smoke test:"
echo "   cd ${OUT} && LD_LIBRARY_PATH=/home/n/.nikolachess/lib ./libxrmgov"
echo "   cd ${OUT} && ./xrm_mind_port --iter 10000"
