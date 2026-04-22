# MIND Port vs Hybrid Bench — Test Harness
# Copyright 2026 STARGA, Inc. STARGA Commercial License.
#
# Prereqs (not shipped in this public repo):
#   mindc       STARGA MIND compiler (internal toolchain)
#   xrm_blob    XRM-SSD V23.3 reflection .so/.cubin (provided by Polo)
#
# This Makefile does NOT fabricate TPS numbers. It produces a
# measurement script; results come from the hardware running it.

MINDC           ?= mindc
XRM_BLOB        ?= ../libs/xrm_ssd_v23_3.so
SEAL_HEADER     := seal_hashes.inc
TARGET          ?= l4-cuda
OUT             := xrm_mind_port
SRC             := ../examples/xrm_mind_port.mind
PROTECTION      := ../protection/Mind.toml
EXPORTS_MAP     := ../protection/exports.map
HYBRID_DIR      ?= ../xrm_ssd_v23_3_integration
HYBRID_STDOUT   := hybrid_stdout.log
ITERATIONS      ?= 1000000
RATIOS          ?= 100,500,1000,2500,5000

.PHONY: all seal build run compare clean check check-env \
        run-hybrid run-hybrid-capture parse-hybrid help \
        verify-replay show-capabilities

help:
	@echo "Targets:"
	@echo "  make check-env             Report which prereqs are present."
	@echo "  make run-hybrid            Run Polo's main.rs hybrid (prints TPS)."
	@echo "  make run-hybrid-capture    Same, but also tees stdout to $(HYBRID_STDOUT)."
	@echo "  make parse-hybrid          Turn $(HYBRID_STDOUT) into results_hybrid.json."
	@echo "  make seal                  SHA-256 the XRM blob into $(SEAL_HEADER)."
	@echo "  make build                 Compile the MIND port ELF (needs mindc)."
	@echo "  make run                   Run the MIND port ELF → results_mind.json."
	@echo "  make verify-replay         Run sweep twice, assert chain roots match (determinism)."
	@echo "  make compare               Emit comparison.md (uses whichever JSON exists)."
	@echo "  make show-capabilities     Print the MIND-vs-hybrid capability map."
	@echo "  make clean                 Remove build / seal / results / log files."

all: check seal build

# ---------------- Diagnostics ----------------

check-env:
	@echo "== Environment check =="
	@printf "mindc:             "; command -v $(MINDC) >/dev/null 2>&1 && \
	    $(MINDC) --version 2>&1 || echo "not found (STARGA toolchain required for MIND build)"
	@printf "cargo:             "; command -v cargo >/dev/null 2>&1 && \
	    cargo --version || echo "not found (needed for hybrid baseline)"
	@printf "python3:           "; command -v python3 >/dev/null 2>&1 && \
	    python3 --version || echo "not found"
	@printf "sha256sum:         "; command -v sha256sum >/dev/null 2>&1 && \
	    echo "present" || echo "not found"
	@printf "XRM_BLOB=%-20s " "$(XRM_BLOB)"; test -f "$(XRM_BLOB)" && \
	    echo "present ($$(stat -c %s "$(XRM_BLOB)" 2>/dev/null || stat -f %z "$(XRM_BLOB)") bytes)" || \
	    echo "not found"
	@printf "HYBRID_DIR=%-18s " "$(HYBRID_DIR)"; test -d "$(HYBRID_DIR)" && \
	    echo "present" || echo "not found (hybrid baseline not reachable)"
	@echo "== End =="

check:
	@command -v $(MINDC) >/dev/null 2>&1 || { \
	  echo "ERROR: mindc not on PATH. Locked build requires STARGA toolchain."; \
	  echo "       Run 'make check-env' to see which prereqs are missing."; \
	  exit 1; }
	@test -f $(XRM_BLOB) || { \
	  echo "ERROR: XRM blob not found at $(XRM_BLOB)."; \
	  echo "       Export XRM_BLOB=/path/to/libxrm_ssd_v23_3.so"; \
	  exit 1; }

# ---------------- Seal ----------------

seal: $(SEAL_HEADER)

$(SEAL_HEADER): $(XRM_BLOB)
	@test -f $(XRM_BLOB) || { echo "ERROR: XRM_BLOB not set or missing."; exit 1; }
	@./seal_blob.sh $(XRM_BLOB) > $(SEAL_HEADER)
	@echo "Sealed $(XRM_BLOB) -> $(SEAL_HEADER)"
	@cat $(SEAL_HEADER)

# ---------------- MIND-side build + run ----------------

build: $(OUT)

$(OUT): $(SRC) $(SEAL_HEADER) $(PROTECTION) $(EXPORTS_MAP)
	$(MINDC) build \
	  --config $(PROTECTION) \
	  --target $(TARGET) \
	  --seal-include $(SEAL_HEADER) \
	  --exports-map $(EXPORTS_MAP) \
	  --opt-level 3 \
	  -o $(OUT) \
	  $(SRC)

# Run the MIND port sweep and emit results_mind.json.
run: $(OUT)
	@echo "=== MIND Port: $(OUT) ==="
	./$(OUT) --iterations $(ITERATIONS) --ratios $(RATIOS) --json results_mind.json
	@echo "Wrote results_mind.json"

# Run the sweep twice, assert that every per-ratio chain root matches.
# Exit 0 = deterministic replay holds. Non-zero = divergence.
# This is the structural guarantee the hybrid main.rs path cannot make.
verify-replay: $(OUT)
	@echo "=== MIND Port: deterministic replay check ==="
	./$(OUT) --iterations $(ITERATIONS) --ratios $(RATIOS) \
	    --json results_replay_a.json
	./$(OUT) --iterations $(ITERATIONS) --ratios $(RATIOS) \
	    --json results_replay_b.json
	python3 scripts/assert_chain_equal.py \
	    results_replay_a.json results_replay_b.json
	@echo "OK: chain roots match across both runs."

# ---------------- Hybrid baseline ----------------

# Runs Polo's existing main.rs. Prints to stdout only (his original behavior).
run-hybrid:
	@echo "=== Hybrid (main.rs) baseline ==="
	@test -d $(HYBRID_DIR) || { \
	  echo "ERROR: $(HYBRID_DIR) not found."; \
	  echo "       Set HYBRID_DIR=/path/to/xrm_ssd_v23_3_integration"; exit 1; }
	cd $(HYBRID_DIR) && cargo run --release

# Runs main.rs and tees stdout to a log we can parse.
run-hybrid-capture:
	@echo "=== Hybrid (main.rs) baseline, capturing to $(HYBRID_STDOUT) ==="
	@test -d $(HYBRID_DIR) || { \
	  echo "ERROR: $(HYBRID_DIR) not found."; \
	  echo "       Set HYBRID_DIR=/path/to/xrm_ssd_v23_3_integration"; exit 1; }
	cd $(HYBRID_DIR) && cargo run --release 2>&1 | tee $(CURDIR)/$(HYBRID_STDOUT)
	@echo "Captured hybrid stdout -> $(HYBRID_STDOUT)"

# Parse the captured stdout into results_hybrid.json.
parse-hybrid:
	@test -f $(HYBRID_STDOUT) || { \
	  echo "ERROR: $(HYBRID_STDOUT) not found. Run 'make run-hybrid-capture' first."; \
	  exit 1; }
	python3 scripts/parse_hybrid_stdout.py \
	  --iterations $(ITERATIONS) \
	  $(HYBRID_STDOUT) results_hybrid.json
	@echo "Wrote results_hybrid.json"

# ---------------- Comparison ----------------

# Emits comparison.md from whatever JSON is present. Missing side shows
# 'not measured' — no projected numbers.
compare:
	@python3 compare.py \
	  --mind results_mind.json \
	  --hybrid results_hybrid.json \
	  --out comparison.md
	@echo "---"
	@cat comparison.md

# ---------------- Cleanup ----------------

clean:
	rm -f $(OUT) $(SEAL_HEADER) \
	      results_mind.json results_hybrid.json comparison.md \
	      results_replay_a.json results_replay_b.json \
	      $(HYBRID_STDOUT)

# ---------------- Reference ----------------

show-capabilities:
	@echo "See CAPABILITIES.md at the repo root for the full map."
	@echo ""
	@sed -n '/## Side-by-side capability map/,/## The four/p' \
	    ../CAPABILITIES.md | sed '$$d'
