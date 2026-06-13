/* Copyright 2026 STARGA, Inc. / Dollarchip — XRM-SSD.
 *
 * Thin wrapper over the M2354 on-chip crypto accelerator (AES/SHA/ECC).
 *
 * The M2354 exposes a hardware SHA engine via the Nuvoton BSP (the CRYPTO
 * peripheral, driven through StdDriver/crypto.c). The two functions below
 * are the only surface the RGE evidence chain needs: a one-shot SHA-256 and
 * a verify key held in TrustZone secure-world.
 *
 * BUILD NOTE: link against the Nuvoton M2354 BSP. The reference impl in
 * crypto_accel.c calls the BSP SHA driver when M2354_BSP is defined, and
 * falls back to the bundled portable SHA-256 (sha256.c) otherwise — so the
 * host build and unit tests compute the SAME digest as the silicon. That
 * identity is the whole point: host-MIND and on-orbit-M2354 must agree.
 */
#ifndef XRM_CRYPTO_ACCEL_H
#define XRM_CRYPTO_ACCEL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Initialize the crypto accelerator (no-op on the portable fallback). */
void crypto_accel_init(void);

/* One-shot SHA-256. out must be 32 bytes. Uses the M2354 SHA engine when
 * built with -DM2354_BSP, else the portable software implementation. */
void crypto_sha256(const uint8_t *data, size_t len, uint8_t out[32]);

/* TrustZone-M secure-world verify: compares an attested digest against the
 * expected value held behind the secure boundary. The non-secure app can
 * REQUEST a verify but can never read the expected digest. On the portable
 * build this is a constant-time memcmp against a caller-provided expected. */
bool crypto_verify_digest_secure(const uint8_t got[32], const uint8_t expected[32]);

#endif /* XRM_CRYPTO_ACCEL_H */
