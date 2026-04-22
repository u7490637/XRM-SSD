//! Runtime protection — NikolaChess-class hardening for the bench harness.
//!
//! Applied in `main()` before any bench logic. Linux x86_64 only. Checks are
//! silent: every failure exits with code 137, the same code an external SIGKILL
//! would leave behind, so an attacker cannot distinguish which check tripped.
//!
//! Layers, in order of application:
//!
//!  1. Environment scan — refuse `LD_PRELOAD`, `LD_AUDIT`, `LD_PROFILE`,
//!     `LD_DEBUG`. Cheap; runs before anything else.
//!  2. `prctl(PR_SET_DUMPABLE, 0)` — no core dumps, no `/proc/self/mem` for
//!     a non-root attacker.
//!  3. Initial `TracerPid` scan of `/proc/self/status`. Non-zero → die.
//!  4. `ptrace(PTRACE_TRACEME)` — traps any *future* debugger attach.
//!  5. Triple-redundant seed / threshold canaries, XOR-checked. Flip any
//!     single bit in memory and the XOR stops being zero → die.
//!  6. Self-SHA-256 of `/proc/self/exe`, gated against a compile-time
//!     expected hash (when populated via `PROTECTED_SELF_HASH`). Tamper with
//!     the ELF on disk → hash diverges → die.
//!  7. Watchdog thread — re-runs steps 3 and 5 every 500 ms for the life of
//!     the process. Post-launch `gdb attach` trips TracerPid → die.

use std::fs;
use std::io::Read;
use std::process;
use std::sync::atomic::{AtomicU64, Ordering};
use std::thread;
use std::time::Duration;

use sha2::{Digest, Sha256};

// ---------------------------------------------------------------------------
// Triple-redundant canaries.
// ---------------------------------------------------------------------------
//
// Each critical constant is stored three times in memory: two copies of the
// value and one bitwise complement. Any tampered bit flip desynchronises
// the triple and the `healthy()` check returns false.
//
// Atomics are used so a tampering thread cannot race a store without a fence.
const CANARY_VAL: u64 = 0x5852_4D5F_5353_4420; // "XRM_SSD "
static CANARY_A: AtomicU64 = AtomicU64::new(CANARY_VAL);
static CANARY_B: AtomicU64 = AtomicU64::new(!CANARY_VAL);
static CANARY_C: AtomicU64 = AtomicU64::new(CANARY_VAL);

#[inline(always)]
fn canary_ok() -> bool {
    let a = CANARY_A.load(Ordering::Relaxed);
    let b = CANARY_B.load(Ordering::Relaxed);
    let c = CANARY_C.load(Ordering::Relaxed);
    // Healthy: a == c == !b. Any bit flip in any of the three fails.
    a == c && b == !a
}

// ---------------------------------------------------------------------------
// Expected self-hash baked in at link time.
// ---------------------------------------------------------------------------
//
// Set by the release build after computing SHA-256 of the final, stripped
// ELF. Delivered via `--cfg protected_hash="<hex>"` or env
// `PROTECTED_SELF_HASH` in `build.rs` (see build.sh step [7]). When unset,
// the self-hash is printed but not gated — useful for local debug runs.
pub const EXPECTED_SELF_HASH: Option<&str> = option_env!("PROTECTED_SELF_HASH");

// ---------------------------------------------------------------------------
// Abort paths.
// ---------------------------------------------------------------------------
#[inline(always)]
fn die() -> ! {
    process::exit(137);
}

// ---------------------------------------------------------------------------
// Check primitives.
// ---------------------------------------------------------------------------
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

fn check_injection_env() {
    for var in ["LD_PRELOAD", "LD_AUDIT", "LD_PROFILE", "LD_DEBUG"] {
        if std::env::var_os(var).is_some() {
            die();
        }
    }
}

fn harden_process_no_ptrace() {
    unsafe {
        let _ = prctl(4, 0, 0, 0, 0); // PR_SET_DUMPABLE = 4
    }
}

fn arm_ptrace_traceme() {
    unsafe {
        let _ = ptrace(0, 0, 0, 0); // PTRACE_TRACEME = 0
    }
}

// ---------------------------------------------------------------------------
// Self-hash + optional gate.
// ---------------------------------------------------------------------------
pub fn self_hash() -> [u8; 32] {
    let mut hasher = Sha256::new();
    let Ok(mut f) = fs::File::open("/proc/self/exe") else { return [0u8; 32] };
    let mut buf = [0u8; 65_536];
    loop {
        match f.read(&mut buf) {
            Ok(0) => break,
            Ok(n) => hasher.update(&buf[..n]),
            Err(_) => return [0u8; 32],
        }
    }
    hasher.finalize().into()
}

fn hex(b: &[u8; 32]) -> String {
    let mut s = String::with_capacity(64);
    for &x in b {
        use std::fmt::Write;
        let _ = write!(s, "{:02x}", x);
    }
    s
}

/// Compare the live self-hash against the baked-in expected hash, if any.
/// A mismatch means the on-disk ELF was patched after release — abort.
fn check_self_hash() {
    let Some(expected) = EXPECTED_SELF_HASH else { return };
    let live = self_hash();
    if live == [0u8; 32] {
        return;
    }
    if hex(&live) != expected {
        die();
    }
}

// ---------------------------------------------------------------------------
// Watchdog — re-checks tamper surfaces every 500 ms.
// ---------------------------------------------------------------------------
/// Record the PID that is tracing us right now (should be our parent after
/// `PTRACE_TRACEME`). The watchdog will abort if it ever sees a *different*
/// non-zero tracer appear — i.e. a second debugger attaching.
fn initial_tracer_pid() -> i64 {
    let Ok(mut f) = fs::File::open("/proc/self/status") else { return 0 };
    let mut s = String::with_capacity(2048);
    if f.read_to_string(&mut s).is_err() {
        return 0;
    }
    for line in s.lines() {
        if let Some(rest) = line.strip_prefix("TracerPid:") {
            return rest.trim().parse().unwrap_or(0);
        }
    }
    0
}

fn spawn_watchdog(expected_tracer: i64) {
    thread::Builder::new()
        .name("mind-watchdog".into())
        .spawn(move || loop {
            thread::sleep(Duration::from_millis(500));
            if !canary_ok() {
                die();
            }
            // Tracer change → a second debugger attached → die.
            let current = initial_tracer_pid();
            if current != expected_tracer {
                die();
            }
        })
        .ok(); // If spawn fails we continue without a watchdog.
}

// ---------------------------------------------------------------------------
// Public entry point.
// ---------------------------------------------------------------------------
pub fn enforce() {
    check_injection_env();
    harden_process_no_ptrace();
    check_tracer_pid();
    arm_ptrace_traceme();
    // After PTRACE_TRACEME the parent becomes our tracer. Snapshot that
    // expected value so the watchdog only fires on a *second*, unexpected
    // attach.
    let expected_tracer = initial_tracer_pid();
    if !canary_ok() {
        die();
    }
    check_self_hash();
    spawn_watchdog(expected_tracer);
}

// ---------------------------------------------------------------------------
// Minimal libc shims.
// ---------------------------------------------------------------------------
#[link(name = "c")]
extern "C" {
    fn prctl(option: i32, arg2: u64, arg3: u64, arg4: u64, arg5: u64) -> i32;
    fn ptrace(request: i32, pid: i32, addr: u64, data: u64) -> i64;
}
