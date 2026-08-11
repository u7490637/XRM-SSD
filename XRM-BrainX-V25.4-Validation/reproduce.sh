#!/usr/bin/env bash
set -euo pipefail

echo "================================================================="
echo " XRM-BrainX V25.4 End-to-End Reproducibility Suite"
echo " Timestamp: $(date -u) | Host: $(hostname)"
echo "================================================================="

# 1. 環境與算力狀態稽核
echo "[1/4] Verifying Environment and Hashes..."
sha256sum -c hashes/input_manifest.sha256

# 2. Stage 66 全局最佳化可重現測試
echo "[2/4] Executing Stage 66 Global Optimality Verification..."
python3 source/stage66_kkt_reproduce.py \
    --config configs/stage66_config.json \
    --seed seeds/global_seed.txt \
    --out raw_results/stage66_run.json

# 3. 跨種子一緻性 (138 Regimes & Jaccard)
echo "[3/4] Running Cross-Seed Robustness Verification..."
python3 source/stage65_jaccard_reproduce.py \
    --seeds seeds/multi_seeds.txt \
    --out raw_results/stage65_run.json

# 4. 比對最終產出數據與哈希
echo "[4/4] Cross-checking Metrics against Golden Claims..."
python3 source/verify_claims.py \
    --golden raw_results/golden_metrics.json \
    --current raw_results/

echo "================================================================="
echo " ✅ REPRODUCIBILITY SUCCESSFUL: All metrics match golden state."
echo "================================================================="
