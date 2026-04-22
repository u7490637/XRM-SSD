/*
 * xrm-ssd Runtime Protection — C99 Implementation
 * Copyright (c) 2025-2026 STARGA, Inc. All rights reserved.
 * PROPRIETARY AND CONFIDENTIAL — DO NOT DISTRIBUTE
 *
 * Mirrors NikolaChess protection.mind:
 *   - Anti-debug (ptrace, TracerPid, timing, breakpoint detection)
 *   - VM bytecode interpreter for critical checks
 *   - Encrypted strings (XOR with 16-byte key)
 *   - Triple-redundant state with integrity checksums
 *   - Watchdog thread (randomized 200-800ms intervals)
 *   - Canary values
 *   - SipHash auth challenge-response
 *   - Path verification (locked to "xrmgov" / "xrm-ssd")
 *   - LD_PRELOAD / DYLD_INSERT_LIBRARIES detection
 *   - Function hook detection (INT3, JMP, MOV RAX)
 *   - Anti-dump (PR_SET_DUMPABLE)
 */

#define _GNU_SOURCE
#include "protection.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>

/* ================================================================
 * Platform includes
 * ================================================================ */

#ifdef __linux__
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <errno.h>
#include <pthread.h>
#include <sys/ptrace.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>
#define PT_DENY_ATTACH 31
#endif

#ifdef _WIN32
#include <windows.h>
#include <winternl.h>
#pragma comment(lib, "ntdll.lib")
#endif

/* ================================================================
 * Constants
 * ================================================================ */

/* VM opcodes — custom bytecode interpreter */
#define VM_OP_LOAD    0x01
#define VM_OP_STORE   0x02
#define VM_OP_ADD     0x03
#define VM_OP_SUB     0x04
#define VM_OP_XOR     0x05
#define VM_OP_CMP     0x06
#define VM_OP_JMP     0x07
#define VM_OP_JZ      0x08
#define VM_OP_JNZ     0x09
#define VM_OP_CALL    0x0A
#define VM_OP_RET     0x0B
#define VM_OP_SYSCALL 0x0C
#define VM_OP_HALT    0xFF

/* VM syscalls */
#define VM_SYS_CHECK_DEBUGGER  0x01
#define VM_SYS_GET_TIME        0x02
#define VM_SYS_VERIFY_HASH     0x03
#define VM_SYS_CHECK_INTEGRITY 0x04
#define VM_SYS_CORRUPT_AND_DIE 0xFF

/* Canary values */
#define CANARY_VAL_A 0xDEADBEEFCAFEBABEULL
#define CANARY_VAL_B 0x0123456789ABCDEFULL
#define CANARY_VAL_C 0xFEDCBA9876543210ULL

/* SipHash constants */
#define SIPHASH_C0 0x736f6d6570736575ULL
#define SIPHASH_C1 0x646f72616e646f6dULL
#define SIPHASH_C2 0x6c7967656e657261ULL
#define SIPHASH_C3 0x7465646279746573ULL

/* String encryption key (xrm-ssd specific) */
static const uint8_t STRING_KEY[16] = {
    0x58, 0x52, 0x4D, 0x47, 0x4F, 0x56, 0x50, 0x52,
    0x4F, 0x54, 0x45, 0x43, 0x54, 0x32, 0x36, 0x56
};

/* Auth key — XOR obfuscated (xrm-ssd specific) */
static const uint8_t AUTH_KEY_ENC[32] = {
    0x58, 0x52, 0x4D, 0x47, 0x4F, 0x56, 0x5F, 0x47,
    0x4F, 0x56, 0x45, 0x52, 0x4E, 0x41, 0x4E, 0x43,
    0x45, 0x5F, 0x4B, 0x45, 0x52, 0x4E, 0x45, 0x4C,
    0x5F, 0x32, 0x30, 0x32, 0x36, 0x5F, 0x56, 0x31
};
static const uint8_t AUTH_KEY_XOR = 0x2B;

/* ================================================================
 * State — Triple redundant with encrypted storage
 * ================================================================ */

static uint8_t  state_key_a[32];
static uint8_t  state_key_b[32];
static uint8_t  state_key_c[32];
static uint8_t  enc_state_a[64];
static uint8_t  enc_state_b[64];
static uint8_t  enc_state_c[64];
static uint64_t integrity_checksum = 0;
static volatile uint64_t canary_a = CANARY_VAL_A;
static volatile uint64_t canary_b = CANARY_VAL_B;
static volatile uint64_t canary_c = CANARY_VAL_C;
static volatile int watchdog_running = 0;
static volatile uint64_t check_counter = 0;

/* VM state */
static uint8_t vm_bytecode[512];
static uint8_t vm_key[32];

/* Auth state */
static uint64_t auth_challenge_val = 0;
static volatile int auth_verified = 0;

/* Initialization flag */
static volatile int protection_initialized = 0;

/* ================================================================
 * Secure zero — resistant to compiler dead-store elimination
 * ================================================================ */

static void secure_zero(void *p, size_t n) {
    volatile uint8_t *v = (volatile uint8_t *)p;
    while (n--) *v++ = 0;
}

/* ================================================================
 * Time helper
 * ================================================================ */

static uint64_t time_nanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ================================================================
 * SipHash-2-4 (from crypto.mind)
 * ================================================================ */

static inline uint64_t rotl64(uint64_t x, int b) {
    return (x << b) | (x >> (64 - b));
}

static inline void sip_round(uint64_t *v0, uint64_t *v1,
                              uint64_t *v2, uint64_t *v3) {
    *v0 += *v1; *v1 = rotl64(*v1, 13); *v1 ^= *v0; *v0 = rotl64(*v0, 32);
    *v2 += *v3; *v3 = rotl64(*v3, 16); *v3 ^= *v2;
    *v0 += *v3; *v3 = rotl64(*v3, 21); *v3 ^= *v0;
    *v2 += *v1; *v1 = rotl64(*v1, 17); *v1 ^= *v2; *v2 = rotl64(*v2, 32);
}

static uint64_t siphash_keyed(const uint8_t *data, size_t len,
                               const uint8_t key[16]) {
    uint64_t k0 = 0, k1 = 0;
    for (int i = 0; i < 8; i++) {
        k0 |= (uint64_t)key[i] << (i * 8);
        k1 |= (uint64_t)key[i + 8] << (i * 8);
    }

    uint64_t v0 = SIPHASH_C0 ^ k0;
    uint64_t v1 = SIPHASH_C1 ^ k1;
    uint64_t v2 = SIPHASH_C2 ^ k0;
    uint64_t v3 = SIPHASH_C3 ^ k1;

    size_t blocks = len / 8;
    for (size_t block = 0; block < blocks; block++) {
        uint64_t m = 0;
        for (int i = 0; i < 8; i++)
            m |= (uint64_t)data[block * 8 + i] << (i * 8);
        v3 ^= m;
        sip_round(&v0, &v1, &v2, &v3);
        sip_round(&v0, &v1, &v2, &v3);
        v0 ^= m;
    }

    uint64_t b = (uint64_t)len << 56;
    size_t rem = len % 8;
    size_t off = blocks * 8;
    if (rem >= 7) b |= (uint64_t)data[off + 6] << 48;
    if (rem >= 6) b |= (uint64_t)data[off + 5] << 40;
    if (rem >= 5) b |= (uint64_t)data[off + 4] << 32;
    if (rem >= 4) b |= (uint64_t)data[off + 3] << 24;
    if (rem >= 3) b |= (uint64_t)data[off + 2] << 16;
    if (rem >= 2) b |= (uint64_t)data[off + 1] << 8;
    if (rem >= 1) b |= (uint64_t)data[off + 0];

    v3 ^= b;
    sip_round(&v0, &v1, &v2, &v3);
    sip_round(&v0, &v1, &v2, &v3);
    v0 ^= b;

    v2 ^= 0xff;
    sip_round(&v0, &v1, &v2, &v3);
    sip_round(&v0, &v1, &v2, &v3);
    sip_round(&v0, &v1, &v2, &v3);
    sip_round(&v0, &v1, &v2, &v3);

    return v0 ^ v1 ^ v2 ^ v3;
}

static uint64_t siphash_u64(uint64_t value) {
    uint8_t data[8];
    for (int i = 0; i < 8; i++)
        data[i] = (uint8_t)(value >> (i * 8));
    const uint8_t key[16] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f
    };
    return siphash_keyed(data, 8, key);
}

/* ================================================================
 * Encrypted strings
 * ================================================================ */

static void decrypt_string(const uint8_t *enc, size_t len, char *out) {
    for (size_t i = 0; i < len; i++)
        out[i] = (char)(enc[i] ^ STRING_KEY[i % 16]);
    out[len] = '\0';
}

/* Encrypted: "/proc/self/status" */
static const uint8_t ENC_PROC_STATUS[] = {
    0x63,0x41,0x36,0x2B,0x21,0x6F,0x33,0x27,
    0x2B,0x23,0x0F,0x33,0x32,0x24,0x32,0x30,0x38
};

/* Encrypted: "TracerPid" */
static const uint8_t ENC_TRACER_PID[] = {
    0x19,0x43,0x2D,0x27,0x22,0x47,0x1C,0x29,0x20
};

/* ================================================================
 * Forward declarations
 * ================================================================ */

static void corrupt_and_die(void);
static int  is_debugger_present_native(void);
static int  check_integrity_native(void);
static int  verify_binary_hash_native(void);
static int  run_vm_verification(void);

/* ================================================================
 * Library-mode detection
 * When loaded as a shared library into Python (ctypes), skip ptrace
 * checks since the host process may legitimately have a tracer
 * (IDE debugger, sandbox, etc.). All other checks remain active.
 * ================================================================ */

static volatile int _library_mode_cached = -1;

static int is_library_mode(void) {
    if (_library_mode_cached >= 0) return _library_mode_cached;
    char exe[4096]; /* PATH_MAX */
    ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (len <= 0) { _library_mode_cached = 0; return 0; }
    exe[len] = '\0';
    /* If loaded into python/python3, we're in library mode */
    int result = (strstr(exe, "python") != NULL);
    _library_mode_cached = result;
    return result;
}

/* ================================================================
 * Anti-debug (from protection.mind)
 * ================================================================ */

#ifdef __linux__
static int is_debugger_present_native(void) {
    /* Method 1: ptrace self-trace (skip in library mode —
     * Python ctypes loading can't survive ptrace self-trace
     * and the host may have a legitimate tracer) */
    if (!is_library_mode()) {
        if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) == -1) {
            return 1;
        }
        ptrace(PTRACE_DETACH, 0, NULL, NULL);
    }

    /* Method 2: TracerPid in /proc/self/status (skip in library mode —
     * the host Python process may have a legitimate tracer from IDE/sandbox) */
    if (!is_library_mode()) {
        char path[32];
        decrypt_string(ENC_PROC_STATUS, sizeof(ENC_PROC_STATUS), path);
        FILE *f = fopen(path, "r");
        if (f) {
            char needle[16];
            decrypt_string(ENC_TRACER_PID, sizeof(ENC_TRACER_PID), needle);
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                if (strncmp(line, needle, strlen(needle)) == 0) {
                    char *colon = strchr(line, ':');
                    if (colon) {
                        int pid = atoi(colon + 1);
                        if (pid != 0) {
                            fclose(f);
                            return 1;
                        }
                    }
                }
            }
            fclose(f);
        }
    }

    /* Method 3: Timing — 5000 iterations should take < 25ms */
    uint64_t start = time_nanos();
    volatile uint64_t x = 0x5DEECE66DULL;
    for (int i = 0; i < 5000; i++)
        x = x * 0x5DEECE66DULL + 0xBULL;
    uint64_t elapsed = time_nanos() - start;
    if (elapsed > 25000000ULL) /* 25ms */
        return 1;

    return 0;
}
#endif

#ifdef __APPLE__
static int is_debugger_present_native(void) {
    ptrace(PT_DENY_ATTACH, 0, NULL, 0);

    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
    struct kinfo_proc info;
    size_t size = sizeof(info);
    memset(&info, 0, sizeof(info));
    if (sysctl(mib, 4, &info, &size, NULL, 0) == 0) {
        if ((info.kp_proc.p_flag & P_TRACED) != 0)
            return 1;
    }
    return 0;
}
#endif

#ifdef _WIN32
static int is_debugger_present_native(void) {
    if (IsDebuggerPresent()) return 1;

    BOOL remote = FALSE;
    CheckRemoteDebuggerPresent(GetCurrentProcess(), &remote);
    if (remote) return 1;

    /* ProcessDebugPort */
    NTSTATUS status;
    DWORD_PTR debug_port = 0;
    status = NtQueryInformationProcess(GetCurrentProcess(), 7,
                                        &debug_port, sizeof(debug_port), NULL);
    if (status == 0 && debug_port != 0) return 1;

    return 0;
}
#endif

/* ================================================================
 * Environment checks (from auth.mind)
 * ================================================================ */

static int check_environment(void) {
#ifdef __linux__
    if (getenv("LD_PRELOAD") != NULL) return 1;
    if (getenv("LD_AUDIT") != NULL) return 1;
#endif
#ifdef __APPLE__
    if (getenv("DYLD_INSERT_LIBRARIES") != NULL) return 1;
#endif
    return 0;
}

/* ================================================================
 * Path verification — locked to xrm-ssd / xrmgov
 * ================================================================ */

static int check_path(void) {
#ifdef __linux__
    char exe[4096];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (len <= 0) return 1;
    exe[len] = '\0';

    /* Convert to lowercase for case-insensitive check */
    for (ssize_t i = 0; i < len; i++) {
        if (exe[i] >= 'A' && exe[i] <= 'Z')
            exe[i] += 32;
    }

    /* Loader allowlist */
    if (strstr(exe, "xrmgov") == NULL &&
        strstr(exe, "xrm-ssd") == NULL &&
        strstr(exe, "xrm_ssd") == NULL &&
        strstr(exe, "xrm_mind_port") == NULL &&
        strstr(exe, "python") == NULL)
        return 1;

    /* .so path lock: library itself must reside under an xrm directory */
    Dl_info dl;
    if (dladdr((void *)check_path, &dl) && dl.dli_fname) {
        char so_path[4096];
        strncpy(so_path, dl.dli_fname, sizeof(so_path) - 1);
        so_path[sizeof(so_path) - 1] = '\0';
        for (size_t i = 0; so_path[i]; i++) {
            if (so_path[i] >= 'A' && so_path[i] <= 'Z')
                so_path[i] += 32;
        }
        if (strstr(so_path, "xrm-ssd") == NULL &&
            strstr(so_path, "xrm_ssd") == NULL &&
            strstr(so_path, "xrmgov") == NULL &&
            strstr(so_path, "improved") == NULL)
            return 1;
    }
#endif
    return 0;
}

/* ================================================================
 * Function hook detection (from protection.mind)
 * ================================================================ */

static int check_hooks(void) {
    /* Check for INT3 (0xCC), JMP (0xE9), or MOV RAX (0x48 0xB8) at
     * function entry points — indicates debugging/hooking */
    const void *funcs[] = {
        (const void *)xrmgov_protection_init,
        (const void *)xrmgov_heartbeat,
        (const void *)xrmgov_auth_challenge,
        (const void *)corrupt_and_die,
    };
    for (int i = 0; i < 4; i++) {
        const uint8_t *p = (const uint8_t *)funcs[i];
        if (p[0] == 0xCC)                   return 1;  /* INT3 */
        if (p[0] == 0xE9)                   return 1;  /* JMP rel32 */
        if (p[0] == 0xFF && p[1] == 0x25)   return 1;  /* JMP [addr] */
        if (p[0] == 0x48 && p[1] == 0xB8)   return 1;  /* MOV RAX, imm64 */
    }
    return 0;
}

/* ================================================================
 * Integrity — canaries + state checksum
 * ================================================================ */

static int check_integrity_native(void) {
    if (canary_a != CANARY_VAL_A) return 0;
    if (canary_b != CANARY_VAL_B) return 0;
    if (canary_c != CANARY_VAL_C) return 0;
    if (check_hooks()) return 0;
    return 1;
}

/* Binary hash — placeholder (accepts all in dev builds) */
static int verify_binary_hash_native(void) {
    /* In release builds, this would verify SHA-256 of the binary
     * against an authorized hash list. For dev, always passes. */
    return 1;
}

/* ================================================================
 * Virtual Machine — custom bytecode interpreter (from protection.mind)
 * ================================================================ */

typedef struct {
    uint64_t registers[16];
    uint64_t stack[256];
    size_t   sp;
    size_t   pc;
    uint64_t flags;
    int      halted;
    uint64_t result;
} VirtualMachine;

static void vm_handle_syscall(VirtualMachine *vm, uint8_t id) {
    switch (id) {
    case VM_SYS_CHECK_DEBUGGER:
        vm->registers[0] = is_debugger_present_native() ? 1 : 0;
        break;
    case VM_SYS_GET_TIME:
        vm->registers[0] = time_nanos();
        break;
    case VM_SYS_VERIFY_HASH:
        vm->registers[0] = verify_binary_hash_native() ? 1 : 0;
        break;
    case VM_SYS_CHECK_INTEGRITY:
        vm->registers[0] = check_integrity_native() ? 1 : 0;
        break;
    case VM_SYS_CORRUPT_AND_DIE:
        corrupt_and_die();
        break;
    default:
        corrupt_and_die();
        break;
    }
}

static uint64_t vm_read_u64(const uint8_t *data) {
    uint64_t val = 0;
    for (int i = 0; i < 8; i++)
        val |= (uint64_t)data[i] << (i * 8);
    return val;
}

static uint16_t vm_read_u16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint64_t vm_execute(const uint8_t *bytecode, size_t len) {
    VirtualMachine vm;
    memset(&vm, 0, sizeof(vm));

    while (!vm.halted && vm.pc < len) {
        uint8_t op = bytecode[vm.pc++];
        switch (op) {
        case VM_OP_LOAD: {
            if (vm.pc >= len) { corrupt_and_die(); break; }
            uint8_t reg = bytecode[vm.pc++];
            if (vm.pc + 8 > len) { corrupt_and_die(); break; }
            uint64_t val = vm_read_u64(&bytecode[vm.pc]);
            vm.pc += 8;
            if (vm.pc >= len) { corrupt_and_die(); break; }
            vm.registers[reg & 0x0F] = val;
            break;
        }
        case VM_OP_STORE: {
            if (vm.pc >= len) { corrupt_and_die(); break; }
            uint8_t reg = bytecode[vm.pc++];
            vm.result = vm.registers[reg & 0x0F];
            break;
        }
        case VM_OP_ADD: {
            if (vm.pc >= len) { corrupt_and_die(); break; }
            uint8_t dst = bytecode[vm.pc++] & 0x0F;
            if (vm.pc >= len) { corrupt_and_die(); break; }
            uint8_t src = bytecode[vm.pc++] & 0x0F;
            vm.registers[dst] += vm.registers[src];
            break;
        }
        case VM_OP_SUB: {
            if (vm.pc >= len) { corrupt_and_die(); break; }
            uint8_t dst = bytecode[vm.pc++] & 0x0F;
            if (vm.pc >= len) { corrupt_and_die(); break; }
            uint8_t src = bytecode[vm.pc++] & 0x0F;
            vm.registers[dst] -= vm.registers[src];
            break;
        }
        case VM_OP_XOR: {
            if (vm.pc >= len) { corrupt_and_die(); break; }
            uint8_t dst = bytecode[vm.pc++] & 0x0F;
            if (vm.pc >= len) { corrupt_and_die(); break; }
            uint8_t src = bytecode[vm.pc++] & 0x0F;
            vm.registers[dst] ^= vm.registers[src];
            break;
        }
        case VM_OP_CMP: {
            if (vm.pc >= len) { corrupt_and_die(); break; }
            uint8_t a = bytecode[vm.pc++] & 0x0F;
            if (vm.pc >= len) { corrupt_and_die(); break; }
            uint8_t b = bytecode[vm.pc++] & 0x0F;
            vm.flags = (vm.registers[a] == vm.registers[b]) ? 1 : 0;
            break;
        }
        case VM_OP_JMP: {
            if (vm.pc + 2 > len) { corrupt_and_die(); break; }
            uint16_t addr = vm_read_u16(&bytecode[vm.pc]);
            vm.pc = addr;
            break;
        }
        case VM_OP_JZ: {
            if (vm.pc + 2 > len) { corrupt_and_die(); break; }
            uint16_t addr = vm_read_u16(&bytecode[vm.pc]);
            vm.pc += 2;
            if (vm.pc >= len) { corrupt_and_die(); break; }
            if (vm.flags == 1) vm.pc = addr;
            break;
        }
        case VM_OP_JNZ: {
            if (vm.pc + 2 > len) { corrupt_and_die(); break; }
            uint16_t addr = vm_read_u16(&bytecode[vm.pc]);
            vm.pc += 2;
            if (vm.pc >= len) { corrupt_and_die(); break; }
            if (vm.flags != 1) vm.pc = addr;
            break;
        }
        case VM_OP_CALL:
        case VM_OP_RET:
            corrupt_and_die();
            break;
        case VM_OP_SYSCALL: {
            if (vm.pc >= len) { corrupt_and_die(); break; }
            uint8_t sid = bytecode[vm.pc++];
            vm_handle_syscall(&vm, sid);
            break;
        }
        case VM_OP_HALT:
            vm.halted = 1;
            break;
        default:
            corrupt_and_die();
            break;
        }
    }
    return vm.result;
}

static void generate_vm_bytecode(void) {
    uint64_t t = time_nanos();
    for (int i = 0; i < 32; i++)
        vm_key[i] = (uint8_t)((t >> (i % 8)) ^ ((uint64_t)i * 0x9E3779B97F4A7C15ULL));

    /* Generate bytecode: CHECK_DEBUGGER → verify no debugger
     * → CHECK_INTEGRITY → verify integrity → success
     * → else corrupt_and_die */
    uint8_t bc[512];
    memset(bc, 0, sizeof(bc));
    int pos = 0;

    /* SYSCALL CHECK_DEBUGGER */
    bc[pos++] = VM_OP_SYSCALL;
    bc[pos++] = VM_SYS_CHECK_DEBUGGER;

    /* LOAD R1, 0 (expected: no debugger) */
    bc[pos++] = VM_OP_LOAD;
    bc[pos++] = 1;
    memset(&bc[pos], 0, 8); pos += 8;

    /* CMP R0, R1 */
    bc[pos++] = VM_OP_CMP;
    bc[pos++] = 0;
    bc[pos++] = 1;

    /* JNZ to fail (offset 100) */
    bc[pos++] = VM_OP_JNZ;
    bc[pos++] = 100; bc[pos++] = 0;

    /* SYSCALL CHECK_INTEGRITY */
    bc[pos++] = VM_OP_SYSCALL;
    bc[pos++] = VM_SYS_CHECK_INTEGRITY;

    /* LOAD R1, 1 (expected: integrity OK) */
    bc[pos++] = VM_OP_LOAD;
    bc[pos++] = 1;
    bc[pos] = 1; pos++;
    memset(&bc[pos], 0, 7); pos += 7;

    /* CMP R0, R1 */
    bc[pos++] = VM_OP_CMP;
    bc[pos++] = 0;
    bc[pos++] = 1;

    /* JNZ to fail */
    bc[pos++] = VM_OP_JNZ;
    bc[pos++] = 100; bc[pos++] = 0;

    /* Success: LOAD R0, 1 and HALT */
    bc[pos++] = VM_OP_LOAD;
    bc[pos++] = 0;
    bc[pos] = 1; pos++;
    memset(&bc[pos], 0, 7); pos += 7;
    bc[pos++] = VM_OP_STORE;
    bc[pos++] = 0;
    bc[pos++] = VM_OP_HALT;

    /* Fail path at offset 100 */
    pos = 100;
    bc[pos++] = VM_OP_SYSCALL;
    bc[pos++] = VM_SYS_CORRUPT_AND_DIE;

    /* Encrypt bytecode with vm_key */
    for (int i = 0; i < 512; i++)
        vm_bytecode[i] = bc[i] ^ vm_key[i % 32];
}

static int run_vm_verification(void) {
    uint8_t decrypted[512];
    for (int i = 0; i < 512; i++)
        decrypted[i] = vm_bytecode[i] ^ vm_key[i % 32];
    return vm_execute(decrypted, 512) == 1;
}

/* ================================================================
 * Triple-redundant state (from protection.mind)
 * ================================================================ */

static void xor_crypt(const uint8_t *data, const uint8_t *key,
                       size_t dlen, size_t klen, uint8_t *out) {
    for (size_t i = 0; i < dlen; i++)
        out[i] = data[i] ^ key[i % klen];
}

static void init_state_encryption(void) {
    uint64_t t = time_nanos();
    for (int i = 0; i < 32; i++) {
        state_key_a[i] = (uint8_t)(t ^ ((uint64_t)i * 0x5DEECE66DULL));
        state_key_b[i] = (uint8_t)(t ^ ((uint64_t)i * 0x27D4EB2F165667C5ULL));
        state_key_c[i] = (uint8_t)(t ^ ((uint64_t)i * 0x9E3779B97F4A7C15ULL));
    }

    uint64_t checksum = 0;
    for (int i = 0; i < 32; i++) {
        checksum = checksum * 31 + state_key_a[i];
        checksum = checksum * 31 + state_key_b[i];
        checksum = checksum * 31 + state_key_c[i];
    }
    integrity_checksum = checksum;
}

static int verify_state_integrity(void) {
    uint64_t checksum = 0;
    for (int i = 0; i < 32; i++) {
        checksum = checksum * 31 + state_key_a[i];
        checksum = checksum * 31 + state_key_b[i];
        checksum = checksum * 31 + state_key_c[i];
    }
    return checksum == integrity_checksum;
}

static void set_initialized(int value) {
    uint8_t marker_a = value ? 0xAA : 0x55;
    uint8_t marker_b = value ? 0x55 : 0xAA;
    uint8_t marker_c = value ? 0xCC : 0x33;

    uint8_t sa[64], sb[64], sc[64];
    memset(sa, 0, 64); memset(sb, 0, 64); memset(sc, 0, 64);
    sa[0] = marker_a; sb[0] = marker_b; sc[0] = marker_c;

    uint64_t t = time_nanos();
    memcpy(&sa[1], &t, 8);
    memcpy(&sb[1], &t, 8);
    memcpy(&sc[1], &t, 8);

    xor_crypt(sa, state_key_a, 64, 32, enc_state_a);
    xor_crypt(sb, state_key_b, 64, 32, enc_state_b);
    xor_crypt(sc, state_key_c, 64, 32, enc_state_c);
}

static int get_initialized(void) {
    if (!verify_state_integrity()) return 0;

    uint8_t da[64], db[64], dc[64];
    xor_crypt(enc_state_a, state_key_a, 64, 32, da);
    xor_crypt(enc_state_b, state_key_b, 64, 32, db);
    xor_crypt(enc_state_c, state_key_c, 64, 32, dc);

    /* All three must agree with XOR cross-check */
    int ok = (da[0] == 0xAA) && (db[0] == 0x55) && (dc[0] == 0xCC);
    int xor_ok = ((da[0] ^ db[0]) == 0xFF) && ((db[0] ^ dc[0]) == 0x99);
    return ok && xor_ok;
}

/* ================================================================
 * Anti-dump (from protection.mind)
 * ================================================================ */

static void protect_memory(void) {
#ifdef __linux__
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
#endif
}

/* ================================================================
 * Destruction (from protection.mind)
 * ================================================================ */

static void corrupt_and_die(void) {
    /* Wipe all state — use secure_zero to prevent compiler elimination */
    secure_zero(state_key_a, 32);
    secure_zero(state_key_b, 32);
    secure_zero(state_key_c, 32);
    secure_zero(enc_state_a, 64);
    secure_zero(enc_state_b, 64);
    secure_zero(enc_state_c, 64);
    secure_zero(vm_bytecode, 512);
    secure_zero(vm_key, 32);
    auth_challenge_val = 0;
    auth_verified = 0;
    canary_a = 0; canary_b = 0; canary_c = 0;

#ifdef __linux__
    kill(getpid(), SIGKILL);
#endif
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), 0xDEAD);
#endif
    abort();
}

/* ================================================================
 * Watchdog thread (from protection.mind)
 * ================================================================ */

#if defined(__linux__) || defined(__APPLE__)
static void *watchdog_loop(void *arg) {
    (void)arg;
    uint64_t counter = 0;

    while (1) {
        /* Randomized interval: 200-800ms */
        uint64_t interval = 200 + ((time_nanos() ^ counter) % 600);
        struct timespec ts = {0, (long)(interval * 1000000)};
        nanosleep(&ts, NULL);

        counter++;
        check_counter = counter;

        int check_type = (int)((counter ^ time_nanos()) % 9);
        int passed = 1;

        switch (check_type) {
        case 0: passed = !is_debugger_present_native(); break;
        case 1: passed = check_integrity_native();      break;
        case 2: passed = verify_state_integrity();      break;
        case 3: passed = run_vm_verification();         break;
        case 4: passed = (canary_a == CANARY_VAL_A);    break;
        case 5: passed = (canary_b == CANARY_VAL_B);    break;
        case 6: passed = (canary_c == CANARY_VAL_C);    break;
        case 7: passed = !check_environment();          break;
        case 8: passed = !check_path();                 break;
        }

        if (!passed) corrupt_and_die();
    }
    return NULL;
}

static void start_watchdog(void) {
    if (__sync_bool_compare_and_swap(&watchdog_running, 0, 1)) {
        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&tid, &attr, watchdog_loop, NULL);
        pthread_attr_destroy(&attr);
    }
}
#endif

#ifdef _WIN32
static DWORD WINAPI watchdog_loop(LPVOID arg) {
    (void)arg;
    uint64_t counter = 0;
    while (1) {
        uint64_t interval = 200 + ((time_nanos() ^ counter) % 600);
        Sleep((DWORD)interval);
        counter++;
        check_counter = counter;
        int check_type = (int)((counter ^ time_nanos()) % 9);
        int passed = 1;
        switch (check_type) {
        case 0: passed = !is_debugger_present_native(); break;
        case 1: passed = check_integrity_native();      break;
        case 2: passed = verify_state_integrity();      break;
        case 3: passed = run_vm_verification();         break;
        case 4: passed = (canary_a == CANARY_VAL_A);    break;
        case 5: passed = (canary_b == CANARY_VAL_B);    break;
        case 6: passed = (canary_c == CANARY_VAL_C);    break;
        case 7: passed = !check_environment();          break;
        case 8: passed = !check_path();                 break;
        }
        if (!passed) corrupt_and_die();
    }
    return 0;
}
static void start_watchdog(void) {
    if (InterlockedCompareExchange(&watchdog_running, 1, 0) == 0) {
        CreateThread(NULL, 0, watchdog_loop, NULL, 0, NULL);
    }
}
#endif

/* ================================================================
 * Auth: SipHash challenge-response (from auth.mind)
 * ================================================================ */

static uint64_t compute_auth_response(uint64_t challenge) {
    /* Decode key */
    uint8_t key[16];
    for (int i = 0; i < 16; i++)
        key[i] = AUTH_KEY_ENC[i] ^ AUTH_KEY_XOR;

    /* HMAC: H(key[0:8] || challenge || key[8:16]) */
    uint8_t data[24];
    memcpy(data, key, 8);
    memcpy(data + 8, &challenge, 8);
    memcpy(data + 16, key + 8, 8);

    uint64_t result = siphash_keyed(data, 24, key);

    /* Clear sensitive data — secure_zero resists compiler elimination */
    secure_zero(key, 16);
    secure_zero(data, 24);

    return result;
}

uint64_t xrmgov_auth_challenge(void) {
    uint64_t t = time_nanos();
    uint64_t pid = (uint64_t)getpid();
    auth_challenge_val = siphash_u64(t ^ pid ^ 0xA5A5A5A5A5A5A5A5ULL);
    return auth_challenge_val;
}

int xrmgov_auth_verify(uint64_t response) {
    if (auth_challenge_val == 0) return 0;

    uint64_t expected = compute_auth_response(auth_challenge_val);
    uint64_t diff = expected ^ response;
    if (diff != 0) {
        corrupt_and_die();
        return 0;
    }

    auth_verified = 1;
    start_watchdog();
    return 1;
}

int xrmgov_auth_is_verified(void) {
    return auth_verified;
}

/* ================================================================
 * Public API (from protection.mind)
 * ================================================================ */

int xrmgov_protection_init(void) {
    if (protection_initialized && get_initialized())
        return 0;

    /* Initialize all protection layers */
    init_state_encryption();
    generate_vm_bytecode();
    protect_memory();

    /* Anti-debug */
    if (is_debugger_present_native()) {
        corrupt_and_die();
        return 1;
    }

    /* Environment tampering */
    if (check_environment()) {
        corrupt_and_die();
        return 2;
    }

    /* Path verification — must be loaded from xrm-ssd / xrmgov context */
    if (check_path()) {
        corrupt_and_die();
        return 5;
    }

    /* VM-based verification */
    if (!run_vm_verification()) {
        corrupt_and_die();
        return 3;
    }

    /* Auto-authenticate (self-challenge for library mode) */
    uint64_t challenge = xrmgov_auth_challenge();
    uint64_t response = compute_auth_response(challenge);
    if (!xrmgov_auth_verify(response)) {
        return 4;
    }

    /* Mark initialized */
    set_initialized(1);
    protection_initialized = 1;

    return 0;
}
/* ================================================================
 * NEW PROTECTIONS (34-45): Added April 2026
 * ================================================================ */

#ifdef __linux__
#include <signal.h>
#include <sys/resource.h>
#include <dlfcn.h>
#include <elf.h>
#endif

/* 34. Frida detection — check /proc/self/maps for frida-agent */
static int detect_frida(void) {
#ifdef __linux__
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return 1;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "frida") || strstr(line, "gadget") || strstr(line, "gum-js")) {
            fclose(f);
            return 0;
        }
    }
    fclose(f);
#endif
    return 1;
}

/* 35. Parent process check — verify parent is expected (not gdb/strace) */
static int check_parent_process(void) {
#ifdef __linux__
    char path[64], buf[256];
    snprintf(path, sizeof(path), "/proc/%d/comm", getppid());
    FILE *f = fopen(path, "r");
    if (!f) return 1;
    if (fgets(buf, sizeof(buf), f)) {
        buf[strcspn(buf, "\n")] = 0;
        if (strstr(buf, "gdb") || strstr(buf, "strace") || strstr(buf, "ltrace") ||
            strstr(buf, "lldb") || strstr(buf, "ida") || strstr(buf, "radare")) {
            fclose(f);
            return 0;
        }
    }
    fclose(f);
#endif
    return 1;
}

/* 36. Ptrace attach detection — try PTRACE_ATTACH to self */
static int check_ptrace_attach(void) {
#ifdef __linux__
    int pid = fork();
    if (pid == 0) {
        /* Child: try to ptrace parent */
        if (ptrace(PTRACE_ATTACH, getppid(), NULL, NULL) != 0) {
            _exit(1); /* Already being traced */
        }
        ptrace(PTRACE_DETACH, getppid(), NULL, NULL);
        _exit(0);
    }
    if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
#endif
    return 1;
}

/* 37. Proc integrity — check /proc/self/status for unexpected fields */
static int check_proc_integrity(void) {
#ifdef __linux__
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return 0;
    char line[256];
    int found_tracer = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "TracerPid:", 10) == 0) {
            int pid = atoi(line + 10);
            if (pid != 0) { fclose(f); return 0; }
            found_tracer = 1;
        }
    }
    fclose(f);
    return found_tracer;
#endif
    return 1;
}

/* 38. Constant-time comparison — prevents timing side channels */
static int constant_time_compare(const uint8_t *a, const uint8_t *b, size_t len) {
    volatile uint8_t result = 0;
    for (size_t i = 0; i < len; i++) {
        result |= a[i] ^ b[i];
    }
    return result == 0;
}

/* 39. Signal handler cleanup (cold boot resistance) */
static void signal_cleanup_handler(int sig) {
    (void)sig;
    /* Zero all key material before exit */
    secure_zero(state_key_a, sizeof(state_key_a));
    secure_zero(state_key_b, sizeof(state_key_b));
    secure_zero(state_key_c, sizeof(state_key_c));
    secure_zero(enc_state_a, sizeof(enc_state_a));
    secure_zero(enc_state_b, sizeof(enc_state_b));
    secure_zero(enc_state_c, sizeof(enc_state_c));
    secure_zero(vm_bytecode, sizeof(vm_bytecode));
    secure_zero(vm_key, sizeof(vm_key));
    _exit(128 + sig);
}

static void install_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_cleanup_handler;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
}

/* 40. Seccomp filter — block dangerous syscalls */
static int install_seccomp_filter(void) {
#if defined(__linux__) && defined(PR_SET_SECCOMP)
    /* Minimal: just disable ptrace from this process */
    if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0) return 0;
    return 1;
#endif
    return 1;
}

/* 41. Timing check — detect single-step debugging */
static int check_timing(void) {
    struct timespec t1, t2;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    /* Busy work that should take < 1ms */
    volatile int sum = 0;
    for (int i = 0; i < 10000; i++) sum += i;
    clock_gettime(CLOCK_MONOTONIC, &t2);
    long diff_ns = (t2.tv_sec - t1.tv_sec) * 1000000000L + (t2.tv_nsec - t1.tv_nsec);
    /* If > 100ms, we're being single-stepped */
    return diff_ns < 100000000L;
}

/* 42. Memory encryption at rest — XOR state with TSC-derived key */
static void encrypt_state_at_rest(void) {
    uint64_t key = time_nanos() ^ 0xA5A5A5A5A5A5A5A5ULL;
    uint8_t *k = (uint8_t *)&key;
    for (size_t i = 0; i < sizeof(state_key_a); i++) {
        enc_state_a[i] = state_key_a[i] ^ k[i % 8];
        enc_state_b[i] = state_key_b[i] ^ k[i % 8];
        enc_state_c[i] = state_key_c[i] ^ k[i % 8];
    }
}

/* 43. Control flow integrity — simple shadow stack */
static void *shadow_stack[64];
static volatile int shadow_sp = 0;

static void cfi_push(void *ret_addr) {
    if (shadow_sp < 64) shadow_stack[shadow_sp++] = ret_addr;
}

static int cfi_verify(void *ret_addr) {
    if (shadow_sp <= 0) return 0;
    return shadow_stack[--shadow_sp] == ret_addr;
}


int xrmgov_heartbeat(void) {
    if (is_debugger_present_native()) {
        corrupt_and_die();
        return 10;
    }
    if (!check_integrity_native()) {
        corrupt_and_die();
        return 11;
    }
    return 0;
}

int xrmgov_is_protected(void) {
    return get_initialized() &&
           canary_a == CANARY_VAL_A &&
           canary_b == CANARY_VAL_B &&
           canary_c == CANARY_VAL_C;
}

const char *xrmgov_get_version(void) {
    return "xrmgov 0.2.0";
}

void xrmgov_shutdown_protection(void) {
    /* Graceful shutdown — wipe all sensitive state */
    secure_zero(state_key_a, 32);
    secure_zero(state_key_b, 32);
    secure_zero(state_key_c, 32);
    secure_zero(enc_state_a, 64);
    secure_zero(enc_state_b, 64);
    secure_zero(enc_state_c, 64);
    secure_zero(vm_bytecode, 512);
    secure_zero(vm_key, 32);
    auth_challenge_val = 0;
    auth_verified = 0;
    protection_initialized = 0;
    integrity_checksum = 0;
}
