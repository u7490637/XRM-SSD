/* Copyright 2026 STARGA, Inc. / Dollarchip — XRM-SSD.
 *
 * Crypto-accelerator wrapper for the M2354.
 *
 *   -DM2354_BSP : route SHA-256 through the Nuvoton hardware SHA engine.
 *   (default)   : portable software SHA-256 (sha256.c) so the host build
 *                 and CI compute the identical digest.
 */
#include "crypto_accel.h"
#include <string.h>

#ifdef M2354_BSP
/* Nuvoton M2354 BSP. Adjust the include to your BSP layout; the StdDriver
 * CRYPTO module exposes a one-shot SHA helper. */
#include "NuMicro.h"

void crypto_accel_init(void) {
    /* Enable the CRYPTO peripheral clock. (CLK_EnableModuleClock is part of
     * the BSP; Sys_Init typically already does this — left explicit here.) */
    CLK_EnableModuleClock(CRPT_MODULE);
    SHA_ENABLE_INT(CRPT);
}

void crypto_sha256(const uint8_t *data, size_t len, uint8_t out[32]) {
    /* Drive the hardware SHA-256 engine. The exact calls depend on the BSP
     * version; the canonical sequence is Open -> SetDMATransfer -> Start ->
     * poll -> Read. Wrapped in a helper in your BSP integration. */
    SHA_Open(CRPT, SHA_MODE_SHA256, SHA_IN_OUT_SWAP, 0);
    SHA_SetDMATransfer(CRPT, (uint32_t)(uintptr_t)data, (uint32_t)len);
    SHA_Start(CRPT, CRYPTO_DMA_ONE_SHOT);
    while (SHA_GET_INT_FLAG(CRPT) == 0) { /* wait */ }
    SHA_CLR_INT_FLAG(CRPT);
    uint32_t digest[8];
    SHA_Read(CRPT, digest);
    for (int i = 0; i < 8; i++) {
        out[i*4+0] = (uint8_t)(digest[i] >> 24);
        out[i*4+1] = (uint8_t)(digest[i] >> 16);
        out[i*4+2] = (uint8_t)(digest[i] >> 8);
        out[i*4+3] = (uint8_t)(digest[i]);
    }
}

#else  /* portable software fallback — identical digest to the silicon */
#include "sha256.h"

void crypto_accel_init(void) { /* no-op */ }

void crypto_sha256(const uint8_t *data, size_t len, uint8_t out[32]) {
    sha256_oneshot(data, len, out);
}
#endif

/* Constant-time comparison so a verify never leaks timing about the
 * expected digest. On the real device the expected value is held in
 * TrustZone secure-world; the non-secure app calls in via a secure gateway
 * and only receives the boolean. */
bool crypto_verify_digest_secure(const uint8_t got[32], const uint8_t expected[32]) {
    uint8_t diff = 0;
    for (int i = 0; i < 32; i++) diff |= (uint8_t)(got[i] ^ expected[i]);
    return diff == 0;
}
