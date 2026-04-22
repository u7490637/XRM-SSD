//! Integration test: evidence chain root is byte-identical across runs.
//!
//! Builds the release binary, invokes it twice with identical args, and
//! asserts the `sweep evidence chain root:` line matches byte-for-byte.
//! This is the harness-level determinism guarantee the bench depends on.
//!
//! Run: `cargo test --release --test determinism`

use std::process::Command;

fn locate_binary() -> std::path::PathBuf {
    let mut p = std::env::current_exe().expect("current_exe");
    while p.file_name().map(|n| n != "release").unwrap_or(false) {
        if !p.pop() {
            panic!("could not locate target/*/release in {:?}", std::env::current_exe());
        }
    }
    let mut candidates = vec![p.join("xrm_mind_port")];
    if let Some(parent) = p.parent() {
        candidates.push(parent.join("release").join("xrm_mind_port"));
    }
    for c in &candidates {
        if c.exists() {
            return c.clone();
        }
    }
    panic!(
        "xrm_mind_port binary not found; build with `cargo build --release` first. \
         Tried: {:?}",
        candidates
    );
}

fn run_and_extract_root(bin: &std::path::Path, args: &[&str]) -> String {
    let out = Command::new(bin)
        .args(args)
        .output()
        .expect("failed to spawn xrm_mind_port");
    assert!(
        out.status.success(),
        "xrm_mind_port exited non-zero: {:?}",
        out.status
    );
    let stdout = String::from_utf8_lossy(&out.stdout);
    for line in stdout.lines() {
        if let Some(rest) = line.strip_prefix("sweep evidence chain root: ") {
            return rest.trim().to_string();
        }
    }
    panic!(
        "no 'sweep evidence chain root' line in stdout:\n{}",
        stdout
    );
}

#[test]
fn evidence_root_is_byte_identical_across_runs() {
    let bin = locate_binary();
    let a = run_and_extract_root(&bin, &["--iter", "10000"]);
    let b = run_and_extract_root(&bin, &["--iter", "10000"]);
    assert_eq!(
        a, b,
        "evidence chain root drifted between runs — determinism guarantee broken"
    );
    assert_eq!(a.len(), 64, "root must be 32-byte SHA-256 hex");
}

#[test]
fn thread_count_does_not_change_root() {
    let bin = locate_binary();
    let serial = run_and_extract_root(&bin, &["--iter", "10000", "--threads", "1"]);
    let parallel = run_and_extract_root(&bin, &["--iter", "10000", "--threads", "5"]);
    assert_eq!(
        serial, parallel,
        "parallel sweep must produce identical root to serial — \
         per-ratio work is independent by design"
    );
}

#[test]
fn different_iter_count_changes_root() {
    let bin = locate_binary();
    let small = run_and_extract_root(&bin, &["--iter", "5000"]);
    let large = run_and_extract_root(&bin, &["--iter", "10000"]);
    assert_ne!(
        small, large,
        "root must differ when iteration count differs — otherwise the chain \
         is not covering the work it claims to"
    );
}
