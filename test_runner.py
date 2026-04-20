#!/usr/bin/env python3
"""
XRM-SSD V23.3 性能測試執行器
適用於 Lightning AI + NVIDIA L4 GPU + TensorRT-LLM
"""

import time
import argparse
import json
from datetime import datetime
import torch
import numpy as np
from tqdm import tqdm
import psutil
import os

# ==================== 配置區 ====================
DEFAULT_CONFIG = {
    "model_version": "XRM-SSD V23.3",
    "backend": "tensorrt_llm",
    "precision": "fp16",           # 可改成 "int8", "fp8"
    "gpu": "NVIDIA L4",
    "batch_sizes": [1, 4, 8, 16, 32],
    "input_length": 128,
    "output_length": 128,
    "num_warmup": 10,
    "num_runs": 100,
    "save_results": True,
    "results_dir": "results"
}

# ==================== 主測試類別 ====================
class XRMSSDTestRunner:
    def __init__(self, config=None):
        self.config = {**DEFAULT_CONFIG, **(config or {})}
        self.device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
        self.model = None
        self.results = {
            "timestamp": datetime.now().isoformat(),
            "config": self.config,
            "hardware": self.get_hardware_info(),
            "test_results": []
        }
        
        print(f"🚀 初始化 XRM-SSD V23.3 測試執行器")
        print(f"   GPU: {torch.cuda.get_device_name(0) if torch.cuda.is_available() else 'CPU'}")
        print(f"   Backend: {self.config['backend']} | Precision: {self.config['precision']}")

    def get_hardware_info(self):
        """取得硬體資訊"""
        info = {
            "gpu": torch.cuda.get_device_name(0) if torch.cuda.is_available() else "N/A",
            "gpu_memory_gb": torch.cuda.get_device_properties(0).total_memory / 1024**3 if torch.cuda.is_available() else 0,
            "cpu_cores": psutil.cpu_count(logical=False),
            "ram_gb": psutil.virtual_memory().total / 1024**3
        }
        return info

    def load_model(self):
        """載入 XRM-SSD V23.3 模型 + TensorRT-LLM backend"""
        print("🔄 正在載入 XRM-SSD V23.3 模型...")
        
        try:
            # TODO: 請根據你的實際 XRM-SSD 載入方式修改這段
            # 範例（請替換成你的真實 import 和初始化）
            from xrm_ssd import XRMSSD_V23   # 如果你有安裝成 package
            
            self.model = XRMSSD_V23(
                version="23.3",
                backend=self.config["backend"],
                precision=self.config["precision"],
                device=self.device
            )
            
            # 如果需要 build TensorRT engine
            # self.model.build_engine(...)  
            
            print("✅ 模型載入成功")
            
        except Exception as e:
            print(f"❌ 模型載入失敗: {e}")
            print("   請確認 XRM-SSD 程式碼已正確放在 repo 中，並修改載入方式")
            raise

    def run_benchmark(self, batch_size):
        """單一 batch size 性能測試"""
        print(f"\n📊 開始測試 Batch Size = {batch_size}")
        
        # 準備測試輸入（請根據你的模型輸入類型調整）
        # 這裡用隨機 tensor 作為範例，實際請改成你的影像/文字輸入
        inputs = [torch.randn(1, 3, 224, 224).to(self.device) for _ in range(batch_size)]
        
        latencies = []
        
        # Warmup
        for _ in range(self.config["num_warmup"]):
            with torch.no_grad():
                _ = self.model.inference(inputs) if hasattr(self.model, "inference") else self.model(inputs)
        
        # 正式測試
        for _ in tqdm(range(self.config["num_runs"]), desc=f"Batch {batch_size}"):
            torch.cuda.synchronize()
            start = time.perf_counter()
            
            with torch.no_grad():
                output = self.model.inference(inputs) if hasattr(self.model, "inference") else self.model(inputs)
            
            torch.cuda.synchronize()
            end = time.perf_counter()
            
            latencies.append((end - start) * 1000)  # ms
        
        avg_latency = np.mean(latencies)
        p95_latency = np.percentile(latencies, 95)
        throughput = (batch_size * 1000) / avg_latency   # TPS (Tokens/Frames per second)
        
        result = {
            "batch_size": batch_size,
            "avg_latency_ms": round(avg_latency, 3),
            "p95_latency_ms": round(p95_latency, 3),
            "throughput_tps": round(throughput, 2),
            "gpu_util": torch.cuda.utilization(0),
            "gpu_memory_used_gb": torch.cuda.max_memory_allocated(0) / 1024**3
        }
        
        print(f"   Avg Latency : {result['avg_latency_ms']:.3f} ms")
        print(f"   P95 Latency : {result['p95_latency_ms']:.3f} ms")
        print(f"   Throughput  : {result['throughput_tps']:.2f} TPS")
        
        return result

    def run_all_tests(self):
        """執行所有 batch size 測試"""
        if self.model is None:
            self.load_model()
        
        print("\n=== 開始 XRM-SSD V23.3 完整性能測試 ===")
        
        for bs in self.config["batch_sizes"]:
            res = self.run_benchmark(bs)
            self.results["test_results"].append(res)
        
        self.save_results()
        self.print_summary()

    def save_results(self):
        """儲存測試結果為 JSON"""
        if not self.config["save_results"]:
            return
            
        os.makedirs(self.config["results_dir"], exist_ok=True)
        filename = f"results/xrm_ssd_v23.3_{self.config['precision']}_{datetime.now().strftime('%Y%m%d_%H%M')}.json"
        
        with open(filename, "w", encoding="utf-8") as f:
            json.dump(self.results, f, indent=2, ensure_ascii=False)
        
        print(f"\n💾 測試結果已儲存至：{filename}")

    def print_summary(self):
        """列印摘要表格"""
        print("\n" + "="*80)
        print("🎯 XRM-SSD V23.3 在 L4 GPU 上的性能測試總結")
        print("="*80)
        print(f"{'Batch Size':<12} {'Avg Latency (ms)':<18} {'P95 Latency (ms)':<18} {'Throughput (TPS)':<18}")
        print("-"*80)
        
        for r in self.results["test_results"]:
            print(f"{r['batch_size']:<12} {r['avg_latency_ms']:<18.3f} {r['p95_latency_ms']:<18.3f} {r['throughput_tps']:<18.2f}")
        
        print("="*80)


# ==================== 主程式 ====================
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="XRM-SSD V23.3 Performance Test Runner")
    parser.add_argument("--precision", type=str, default="fp16", choices=["fp16", "int8", "fp8"])
    parser.add_argument("--num_runs", type=int, default=100)
    args = parser.parse_args()

    config = {
        "precision": args.precision,
        "num_runs": args.num_runs
    }

    runner = XRMSSDTestRunner(config)
    runner.run_all_tests()