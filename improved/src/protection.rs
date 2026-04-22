//! Runtime protection — anti-debug, anti-tamper, integrity attestation.
//!
//! Applied in `main()` before any bench logic. Linux x86_64 only.
//! If a check fails the process exits with code 137 (-SIGKILL equivalent)
//! and prints nothing that would help an attacker identify which check tripped.

use std::fs;
use std::io::Read;
use std::process;

use sha2::{Digest, Sha256};

/// Hard exit without flushing stdio or running destructors.
#[inline(always)]
fn die() -> ! {
    process::exit(137);
}

/// Abort if a debugger is attached via ptrace.
fn check_tracer_pid() {
    let Ok(mut f) = fs::File::open("/proc/self/status") else { return };
    let mut s = String::with_capacity(2048);
    if f.read_to_string(&mut s).is_err() {
        return;
    }
    for line in s.lines() {
        if let Some(rest) = line.strip_prefix("TracerPid:") {
            let pid: i64 = rest.trim().parse().unwrap_or(0);
            if pid != 0 {
                die();
            }
            return;
        }
    }
}

/// Abort if common injection env vars are set.
fn check_injection_env() {
    for var in ["LD_PRELOAD", "LD_AUDIT", "LD_PROFILE", "LD_DEBUG"] {
        if std::env::var_os(var).is_some() {
            die();
        }
    }
}

/// Make this process undumpable (no core, no /proc/self/mem for non-root).
fn harden_process_no_ptrace() {
    unsafe {
        // prctl(PR_SET_DUMPABLE, 0)
        let _ = libc_prctl(4, 0, 0, 0, 0);
    }
}

/// Arm PTRACE_TRACEME. Must be called AFTER `check_tracer_pid`, because
/// success of this call sets TracerPid to the parent PID and would trip
/// a subsequent TracerPid check.
fn arm_ptrace_traceme() {
    unsafe {
        // ptrace(PTRACE_TRACEME, 0, 0, 0) — traps any later attach
        let _ = libc_ptrace(0, 0, 0, 0);
    }
}

/// Attestation: SHA-256 of /proc/self/exe. Printed once at startup.
/// Not a gate (self-hash can always be relocated by an attacker) but gives
/// the operator a cryptographic handle for reproducibility comparisons.
pub fn self_hash() -> [u8; 32] {
    let mut hasher = Sha256::new();
    let Ok(mut f) = fs::File::open("/proc/self/exe") else { return [0u8; 32] };
    let mut buf = [0u8; 65536];
    loop {
        match f.read(&mut buf) {
            Ok(0) => break,
            Ok(n) => hasher.update(&buf[..n]),
            Err(_) => return [0u8; 32],
        }
    }
    hasher.finalize().into()
}

/// Run every protection check. Must be called before main work.
///
/// Order matters:
///   1. env check — cheap, catches preload before anything else
///   2. dumpable=0 — prevents core dumps / /proc/self/mem
///   3. TracerPid check — reads initial state
///   4. PTRACE_TRACEME — traps any *future* debugger attach
pub fn enforce() {
    check_injection_env();
    harden_process_no_ptrace();
    check_tracer_pid();
    arm_ptrace_traceme();
}

// ---------------------------------------------------------------------------
// Minimal libc shims — avoid pulling the `libc` crate dependency.
// ---------------------------------------------------------------------------

#[link(name = "c")]
extern "C" {
    fn prctl(option: i32, arg2: u64, arg3: u64, arg4: u64, arg5: u64) -> i32;
    fn ptrace(request: i32, pid: i32, addr: u64, data: u64) -> i64;
}

#[inline(always)]
unsafe fn libc_prctl(option: i32, arg2: u64, arg3: u64, arg4: u64, arg5: u64) -> i32 {
    prctl(option, arg2, arg3, arg4, arg5)
}

#[inline(always)]
unsafe fn libc_ptrace(request: i32, pid: i32, addr: u64, data: u64) -> i64 {
    ptrace(request, pid, addr, data)
}
