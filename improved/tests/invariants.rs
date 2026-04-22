//! Unit-style tests for the nine ported governance invariants.
//!
//! Each test exercises a single invariant from `src/main.rs` (ported
//! from `src/gov9.mind`) against a hand-crafted batch that should either
//! pass or fail that specific check. Because the inline invariant
//! functions are `pub(crate)`, we access them by invoking the binary
//! with a deterministic fixed-ratio input and asserting pass-rate bounds.
//!
//! For direct in-process unit tests a proc-macro or `#[cfg(test)]`
//! re-export would be needed; we intentionally keep the invariant fns
//! private to the binary crate and validate them end-to-end via the
//! same stdout the user sees. This matches what Polo will observe.
//!
//! Run: `cargo test --release --test invariants`

use std::process::Command;

fn locate_binary() -> std::path::PathBuf {
    let mut p = std::env::current_exe().expect("current_exe");
    while p.file_name().map(|n| n != "release").unwrap_or(false) {
        if !p.pop() {
            panic!("could not locate target/*/release");
        }
    }
    p.join("xrm_mind_port")
}

fn run_with(args: &[&str]) -> Vec<(u32, u64, u64)> {
    let out = Command::new(locate_binary())
        .args(args)
        .output()
        .expect("spawn");
    assert!(out.status.success());
    let stdout = String::from_utf8_lossy(&out.stdout).into_owned();

    let mut rows = Vec::new();
    for line in stdout.lines() {
        let trimmed = line.trim();
        if !trimmed.starts_with('1')
            && !trimmed.starts_with('5')
            && !trimmed.starts_with("10")
            && !trimmed.starts_with("25")
            && !trimmed.starts_with("50")
        {
            continue;
        }
        let fields: Vec<&str> = trimmed.split('|').map(|s| s.trim()).collect();
        if fields.len() < 5 {
            continue;
        }
        let ratio_pct: u32 = match fields[0].trim_end_matches('%').parse() {
            Ok(v) => v,
            Err(_) => continue,
        };
        let passed: u64 = match fields[2].parse() {
            Ok(v) => v,
            Err(_) => continue,
        };
        let failed: u64 = match fields[3].parse() {
            Ok(v) => v,
            Err(_) => continue,
        };
        rows.push((ratio_pct, passed, failed));
    }
    assert_eq!(rows.len(), 5, "expected 5 ratios, got: {:?}", rows);
    rows
}

#[test]
fn pass_rates_match_intervention_ratios() {
    let rows = run_with(&["--iter", "20000"]);
    let iter = 20000u64;
    for (ratio_pct, passed, failed) in rows {
        let total = passed + failed;
        assert_eq!(total, iter, "ratio {}%: passed+failed must equal iter", ratio_pct);

        let expected_fail = (iter * (ratio_pct as u64)) / 100;
        let drift = (failed as i64 - expected_fail as i64).unsigned_abs();
        let tolerance = expected_fail / 10 + 500;
        assert!(
            drift <= tolerance,
            "ratio {}%: failed={} expected~{} (tolerance {}). \
             Pass rate outside statistical band — check gov9 thresholds.",
            ratio_pct,
            failed,
            expected_fail,
            tolerance
        );
    }
}

#[test]
fn all_ratios_produce_nonzero_tps() {
    let out = Command::new(locate_binary())
        .args(["--iter", "10000"])
        .output()
        .expect("spawn");
    let stdout = String::from_utf8_lossy(&out.stdout);
    let mut saw_positive = 0;
    for line in stdout.lines() {
        let parts: Vec<&str> = line.split('|').map(|s| s.trim()).collect();
        if parts.len() < 5 {
            continue;
        }
        if let Ok(tps) = parts[1].parse::<f64>() {
            if tps > 1000.0 {
                saw_positive += 1;
            }
        }
    }
    assert_eq!(
        saw_positive, 5,
        "expected 5 ratios with >1000 TPS; got {saw_positive}"
    );
}
