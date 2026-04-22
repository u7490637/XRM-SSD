/*
 * mind-mem Runtime Protection — C99 Header
 * Copyright (c) 2025-2026 STARGA, Inc. All rights reserved.
 * PROPRIETARY AND CONFIDENTIAL — DO NOT DISTRIBUTE
 *
 * Same protection mechanism as NikolaChess runtime,
 * built specifically for the mind-mem scoring library.
 */

#ifndef XRMGOV_PROTECTION_H
#define XRMGOV_PROTECTION_H

#include <stdint.h>

/* Initialize all protection layers. Must be called before any kernel function.
 * Returns 0 on success, non-zero on protection failure. */
int xrmgov_protection_init(void);

/* Heartbeat — lightweight check called periodically by kernel wrappers.
 * Returns 0 if OK, calls corrupt_and_die() internally if tampered. */
int xrmgov_heartbeat(void);

/* Auth challenge-response (SipHash-based) */
uint64_t xrmgov_auth_challenge(void);
int      xrmgov_auth_verify(uint64_t response);
int      xrmgov_auth_is_verified(void);

/* Check if protection is active and healthy */
int xrmgov_is_protected(void);

/* Version string — returns library version (e.g., "1.4.1") */
const char *xrmgov_get_version(void);

/* Graceful shutdown — wipes sensitive state from memory */
void xrmgov_shutdown_protection(void);

#endif /* XRMGOV_PROTECTION_H */
