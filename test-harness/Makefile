# MIND Port vs Hybrid Bench — Test Harness
# Copyright 2026 STARGA, Inc. STARGA Commercial License.
#
# Prereqs (not shipped in this public repo):
#   mindc       STARGA MIND compiler (internal toolchain)
#   xrm_blob    XRM-SSD V23.3 reflection .so/.cubin (provided by Polo)
#
# This Makefile does NOT fabricate TPS numbers. It produces a
# measurement script; results come from the hardware running it.

MINDC        ?= mindc
XRM_BLOB     ?= ../libs/xrm_ssd_v23_3.so
SEAL_HEADER  := seal_hashes.inc
TARGET       ?= l4-cuda
OUT          := xrm_mind_port
SRC          := ../examples/xrm_mind_port.mind
PROTECTION   := ../protection/Mind.toml
EXPORTS_MAP  := ../protection/exports.map

.PHONY: all seal build run compare clean check

all: check seal build

check:
	@command -v $(MINDC) >/dev/null 2>&1 || { \
	  echo "ERROR: mindc not on PATH. Locked build requires STARGA toolchain."; exit 1; }
	@test -f $(XRM_BLOB) || { \
	  echo "ERROR: XRM blob not found at $(XRM_BLOB)."; \
	  echo "Set XRM_BLOB=/path/to/xrm_ssd_v23_3.so"; exit 1; }

seal: $(SEAL_HEADER)

$(SEAL_HEADER): $(XRM_BLOB)
	@./seal_blob.sh $(XRM_BLOB) > $(SEAL_HEADER)
	@echo "Sealed $(XRM_BLOB)"
	@cat $(SEAL_HEADER)

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

# Run the MIND port sweep (1/5/10/25/50% intervention ratios)
run: $(OUT)
	@echo "=== MIND Port: xrm_mind_port ==="
	./$(OUT) --iterations 1000000 --ratios 100,500,1000,2500,5000

# Run Polo's existing Rust/Python hybrid (main.rs in ../xrm_ssd_v23_3_integration/)
run-hybrid:
	@echo "=== Hybrid (main.rs) baseline ==="
	cd ../xrm_ssd_v23_3_integration && cargo run --release

# Produce side-by-side table. Both commands must have been run first.
compare:
	@python3 compare.py \
	  --mind results_mind.json \
	  --hybrid results_hybrid.json \
	  --out comparison.md
	@cat comparison.md

clean:
	rm -f $(OUT) $(SEAL_HEADER) results_mind.json results_hybrid.json comparison.md
