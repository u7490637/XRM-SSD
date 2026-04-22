//! XRM-SSD + MIND hybrid bench harness.
//!
//! Reimplements the nine governance invariants from `src/gov9.mind` in Rust
//! line-for-line (see `PORTED_FROM:` comments) so the same logic can be
//! measured without pulling an FFI into the mindc-built runtime.
//!
//! The MIND side ships as a sibling binary (`libxrmgov`, built from
//! src/lib.mind + src/gov9.mind by mindc) that demonstrates the kernel
//! compiles and runs. This harness is the measurable bench.
//!
//! Determinism: fixed seed (0x5852_4D5F_5353_4420, "XRM_SSD "). Same binary
//! on same CLI arguments produces a byte-identical evidence chain root on
//! any x86_64 machine.
//!
//! Build:   cargo build --release
//! Run:     ./xrm_mind_port [--iter N] [--batch N] [--features N]

use rand::{RngCore, SeedableRng};
use rand_chacha::ChaCha20Rng;
use sha2::{Digest, Sha256};
use std::time::Instant;

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------
const SEED: u64 = 0x5852_4D5F_5353_4420; // "XRM_SSD "
const DEFAULT_ITER: usize = 1_000_000;
const DEFAULT_BATCH: usize = 128;
const DEFAULT_FEATURES: usize = 16;
const RATIOS_BPS: &[u32] = &[100, 500, 1000, 2500, 5000]; // 1%, 5%, 10%, 25%, 50%

// Governance thresholds — tuned so a healthy batch passes all nine.
const SUM_THRESH: f32 = 1.0e6;
const L2_THRESH: f32 = 1.0e3;
const MEAN_LO: f32 = -1.0;
const MEAN_HI: f32 = 100.0;
const VAR_THRESH: f32 = 1.0e3;
const MAX_THRESH: f32 = 1.0e2;
const COL_RANGE_THRESH: f32 = 1.0e2;

// ---------------------------------------------------------------------------
// Governance invariants — ported from src/gov9.mind
// ---------------------------------------------------------------------------

/// PORTED_FROM: gov9.mind :: inv1_non_negative
#[inline(always)]
fn inv1_non_negative(batch: &[f32]) -> bool {
    batch.iter().all(|&x| x >= 0.0 && x.is_finite())
}

/// PORTED_FROM: gov9.mind :: inv2_sum_bounded
#[inline(always)]
fn inv2_sum_bounded(batch: &[f32], threshold: f32) -> bool {
    let s: f32 = batch.iter().map(|x| x.abs()).sum();
    s < threshold
}

/// PORTED_FROM: gov9.mind :: inv3_l2_bounded
#[inline(always)]
fn inv3_l2_bounded(batch: &[f32], n: usize, d: usize, threshold: f32) -> bool {
    let mut max_norm: f32 = 0.0;
    for i in 0..n {
        let row = &batch[i * d..(i + 1) * d];
        let ss: f32 = row.iter().map(|x| x * x).sum();
        let norm = ss.sqrt();
        if norm > max_norm { max_norm = norm; }
    }
    max_norm < threshold
}

/// PORTED_FROM: gov9.mind :: inv4_mean_in_band
#[inline(always)]
fn inv4_mean_in_band(batch: &[f32], lo: f32, hi: f32) -> bool {
    let s: f32 = batch.iter().sum();
    let m = s / (batch.len() as f32);
    m >= lo && m <= hi
}

/// PORTED_FROM: gov9.mind :: inv5_variance_bounded
#[inline(always)]
fn inv5_variance_bounded(batch: &[f32], threshold: f32) -> bool {
    let n = batch.len() as f32;
    let m: f32 = batch.iter().sum::<f32>() / n;
    let v: f32 = batch.iter().map(|x| (x - m).powi(2)).sum::<f32>() / n;
    v < threshold
}

/// PORTED_FROM: gov9.mind :: inv6_max_bounded
#[inline(always)]
fn inv6_max_bounded(batch: &[f32], threshold: f32) -> bool {
    batch.iter().map(|x| x.abs()).fold(0.0f32, f32::max) < threshold
}

/// PORTED_FROM: gov9.mind :: inv7_row_sums_nonneg
#[inline(always)]
fn inv7_row_sums_nonneg(batch: &[f32], n: usize, d: usize) -> bool {
    for i in 0..n {
        let row = &batch[i * d..(i + 1) * d];
        let s: f32 = row.iter().sum();
        if s < 0.0 { return false; }
    }
    true
}

/// PORTED_FROM: gov9.mind :: inv8_col_range_bounded
#[inline(always)]
fn inv8_col_range_bounded(batch: &[f32], n: usize, d: usize, threshold: f32) -> bool {
    let mut worst: f32 = 0.0;
    for j in 0..d {
        let mut lo = f32::INFINITY;
        let mut hi = f32::NEG_INFINITY;
        for i in 0..n {
            let v = batch[i * d + j];
            if v < lo { lo = v; }
            if v > hi { hi = v; }
        }
        let r = hi - lo;
        if r > worst { worst = r; }
    }
    worst < threshold
}

/// PORTED_FROM: gov9.mind :: inv9_determinism_fence
///
/// Replay fence: the batch total and the sum-of-row-sums must agree within a
/// bounded relative tolerance. Because f32 addition is not associative, the
/// tolerance is relative (1 ulp * batch length) rather than a hard 1e-4.
#[inline(always)]
fn inv9_determinism_fence(batch: &[f32], n: usize, d: usize) -> bool {
    let total: f32 = batch.iter().sum();
    let mut by_row: f32 = 0.0;
    for i in 0..n {
        let row = &batch[i * d..(i + 1) * d];
        by_row += row.iter().sum::<f32>();
    }
    let scale = total.abs().max(1.0);
    let tol = (batch.len() as f32) * f32::EPSILON * scale;
    (total - by_row).abs() <= tol
}

/// PORTED_FROM: gov9.mind :: gov9_evaluate
///
/// Returns a bitmask: bit k set iff invariant k+1 passed.
/// 0x1FF (511) = all nine passed.
#[inline(always)]
fn gov9_evaluate(batch: &[f32], n: usize, d: usize) -> u32 {
    let mut mask = 0u32;
    if inv1_non_negative(batch) { mask |= 1 << 0; }
    if inv2_sum_bounded(batch, SUM_THRESH) { mask |= 1 << 1; }
    if inv3_l2_bounded(batch, n, d, L2_THRESH) { mask |= 1 << 2; }
    if inv4_mean_in_band(batch, MEAN_LO, MEAN_HI) { mask |= 1 << 3; }
    if inv5_variance_bounded(batch, VAR_THRESH) { mask |= 1 << 4; }
    if inv6_max_bounded(batch, MAX_THRESH) { mask |= 1 << 5; }
    if inv7_row_sums_nonneg(batch, n, d) { mask |= 1 << 6; }
    if inv8_col_range_bounded(batch, n, d, COL_RANGE_THRESH) { mask |= 1 << 7; }
    if inv9_determinism_fence(batch, n, d) { mask |= 1 << 8; }
    mask
}

// ---------------------------------------------------------------------------
// Transaction generation
// ---------------------------------------------------------------------------
fn gen_batch(rng: &mut ChaCha20Rng, buf: &mut [f32], healthy: bool) {
    for slot in buf.iter_mut() {
        let u = (rng.next_u32() as f32) / (u32::MAX as f32);
        *slot = if healthy {
            u * 10.0
        } else {
            (u - 0.5) * 20.0 // roughly half negative → trips invariant 1
        };
    }
}

fn bytemuck_cast(buf: &[f32]) -> &[u8] {
    unsafe { std::slice::from_raw_parts(buf.as_ptr() as *const u8, buf.len() * 4) }
}

fn hash_batch(buf: &[f32]) -> [u8; 32] {
    let mut h = Sha256::new();
    h.update(bytemuck_cast(buf));
    h.finalize().into()
}

// ---------------------------------------------------------------------------
// Bench: one intervention ratio
// ---------------------------------------------------------------------------
struct BenchResult {
    tps: f64,
    passed: u64,
    failed: u64,
    evidence_root: [u8; 32],
}

fn bench_one(iter: usize, ratio_bps: u32, n: usize, d: usize) -> BenchResult {
    let _ = ratio_bps; // used via the `rng` seed XOR below
    let mut rng = ChaCha20Rng::seed_from_u64(SEED ^ (ratio_bps as u64));
    let mut batch = vec![0.0f32; n * d];
    let mut evidence = Sha256::new();
    let mut passed = 0u64;
    let mut failed = 0u64;

    let start = Instant::now();
    for i in 0..iter {
        let trigger = (rng.next_u32() % 10_000) < ratio_bps;
        gen_batch(&mut rng, &mut batch, !trigger);

        let mask = gov9_evaluate(&batch, n, d);
        if mask == 0x1FF {
            passed += 1;
        } else {
            failed += 1;
        }

        // Chain every 1024th step.
        if i & 0x3FF == 0 {
            let bh = hash_batch(&batch);
            evidence.update(&bh);
            evidence.update(&mask.to_le_bytes());
        }
    }
    let elapsed = start.elapsed();
    let tps = iter as f64 / elapsed.as_secs_f64();

    BenchResult {
        tps,
        passed,
        failed,
        evidence_root: evidence.finalize().into(),
    }
}

// ---------------------------------------------------------------------------
// CLI
// ---------------------------------------------------------------------------
struct Args {
    iter: usize,
    batch: usize,
    features: usize,
    threads: usize,
}

fn parse_args() -> Args {
    let mut a = Args {
        iter: DEFAULT_ITER,
        batch: DEFAULT_BATCH,
        features: DEFAULT_FEATURES,
        threads: 1,
    };
    let mut it = std::env::args().skip(1);
    while let Some(flag) = it.next() {
        match flag.as_str() {
            "--iter" => a.iter = it.next().and_then(|s| s.parse().ok()).unwrap_or(a.iter),
            "--batch" => a.batch = it.next().and_then(|s| s.parse().ok()).unwrap_or(a.batch),
            "--features" => a.features = it.next().and_then(|s| s.parse().ok()).unwrap_or(a.features),
            "--threads" => a.threads = it.next().and_then(|s| s.parse().ok()).unwrap_or(a.threads).max(1),
            "--help" | "-h" => {
                println!(
                    "usage: xrm_mind_port [--iter N] [--batch N] [--features N] [--threads N]\n\
                     defaults: iter=1,000,000 batch=128 features=16 threads=1\n\
                     --threads >1 fans the 5 ratios out across worker threads.\n\
                     Per-ratio results are independent so the sweep evidence\n\
                     chain root is identical regardless of thread count."
                );
                std::process::exit(0);
            }
            _ => {}
        }
    }
    a
}

fn hex(bytes: &[u8]) -> String {
    let mut s = String::with_capacity(bytes.len() * 2);
    for b in bytes {
        s.push_str(&format!("{:02x}", b));
    }
    s
}

fn run_sweep_serial(args: &Args) -> Vec<(u32, BenchResult)> {
    RATIOS_BPS
        .iter()
        .map(|&r| (r, bench_one(args.iter, r, args.batch, args.features)))
        .collect()
}

fn run_sweep_parallel(args: &Args) -> Vec<(u32, BenchResult)> {
    use std::sync::{Arc, Mutex};
    use std::thread;

    let work: Arc<Mutex<Vec<u32>>> = Arc::new(Mutex::new(RATIOS_BPS.iter().rev().copied().collect()));
    let results: Arc<Mutex<Vec<(u32, BenchResult)>>> = Arc::new(Mutex::new(Vec::new()));
    let n_workers = args.threads.min(RATIOS_BPS.len());

    let mut handles = Vec::with_capacity(n_workers);
    for _ in 0..n_workers {
        let work = Arc::clone(&work);
        let results = Arc::clone(&results);
        let iter = args.iter;
        let batch = args.batch;
        let features = args.features;
        handles.push(thread::spawn(move || loop {
            let next = { work.lock().unwrap().pop() };
            match next {
                Some(ratio) => {
                    let r = bench_one(iter, ratio, batch, features);
                    results.lock().unwrap().push((ratio, r));
                }
                None => break,
            }
        }));
    }
    for h in handles {
        h.join().unwrap();
    }

    let mut out = Arc::try_unwrap(results)
        .unwrap_or_else(|_| panic!("results Arc still shared"))
        .into_inner()
        .unwrap();
    out.sort_by_key(|(r, _)| *r);
    out
}

fn main() {
    let args = parse_args();

    println!("XRM-SSD + MIND hybrid bench (STARGA xrm_mind_port v0.2.0)");
    println!("  gov9 invariants ported from src/gov9.mind (see PORTED_FROM comments)");
    println!("  MIND kernel proof-of-life: run sibling binary ./libxrmgov");
    println!("----------------------------------------------------------------");
    println!(
        "iter={} batch={} features={} threads={} seed=0x{:016x}",
        args.iter, args.batch, args.features, args.threads, SEED
    );
    println!();

    let wall_start = Instant::now();
    let results = if args.threads <= 1 {
        run_sweep_serial(&args)
    } else {
        run_sweep_parallel(&args)
    };
    let wall = wall_start.elapsed();

    let mut chain = Sha256::new();
    println!("  ratio |          TPS |     passed |     failed | evidence_root");
    println!("--------+--------------+------------+------------+----------------");
    for (ratio, r) in &results {
        chain.update(&r.evidence_root);
        println!(
            "  {:>4}% | {:>12.2} | {:>10} | {:>10} | {}",
            ratio / 100,
            r.tps,
            r.passed,
            r.failed,
            &hex(&r.evidence_root)[..16]
        );
    }
    let root: [u8; 32] = chain.finalize().into();
    let total_iters: u64 = (args.iter as u64) * (RATIOS_BPS.len() as u64);
    let agg_tps = total_iters as f64 / wall.as_secs_f64();
    println!();
    println!("sweep evidence chain root: {}", hex(&root));
    println!(
        "wall: {:.2}s   aggregate sweep TPS: {:.2}   threads: {}",
        wall.as_secs_f64(),
        agg_tps,
        args.threads
    );
    println!();
    println!("Replay: same binary + same args ⇒ byte-identical root.");
    println!("--threads only changes wall-time; per-ratio numbers are independent.");
}
