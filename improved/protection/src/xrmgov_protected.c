/*
 * xrm-ssd Protected Governance Library
 * Copyright (c) 2026 STARGA, Inc. All rights reserved.
 * PROPRIETARY AND CONFIDENTIAL — DO NOT DISTRIBUTE
 *
 * Exposes the nine governance invariants (gov9) as a C ABI under the
 * STARGA runtime protection layer. Every exported entry point runs a
 * heartbeat check, and protection auto-initializes on library load.
 *
 * The nine invariants here mirror:
 *   - improved/src/gov9.mind      (mindc tensor reference)
 *   - improved/src/main.rs        (Rust bench harness port)
 *
 * Build: driven by improved/build.sh (5-stage protected pipeline).
 */

#include "protection.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

/* ================================================================
 * Heartbeat gating — amortized to near-zero cost
 * ================================================================ */

static volatile uint64_t kernel_call_counter = 0;
#define HEARTBEAT_INTERVAL 64

static inline void kernel_guard(void) {
    uint64_t c = __sync_fetch_and_add(&kernel_call_counter, 1);
    if ((c & (HEARTBEAT_INTERVAL - 1)) == 0)
        xrmgov_heartbeat();
}

/* ================================================================
 * Auto-init on library load
 * ================================================================ */

__attribute__((constructor))
static void lib_init(void) {
    xrmgov_protection_init();
}

#define EXPORT __attribute__((visibility("default")))

/* ================================================================
 * Nine governance invariants (gov9) — return 1 on pass, 0 on fail
 * ================================================================ */

static int inv1_non_negative(const float *batch, int total) {
    for (int i = 0; i < total; i++) {
        float v = batch[i];
        if (!(v >= 0.0f)) return 0;
        if (!isfinite(v)) return 0;
    }
    return 1;
}

static int inv2_sum_bounded(const float *batch, int total, float threshold) {
    float s = 0.0f;
    for (int i = 0; i < total; i++) {
        float v = batch[i];
        s += v < 0.0f ? -v : v;
    }
    return s < threshold;
}

static int inv3_l2_bounded(const float *batch, int n, int d, float threshold) {
    float max_norm = 0.0f;
    for (int i = 0; i < n; i++) {
        const float *row = batch + i * d;
        float ss = 0.0f;
        for (int j = 0; j < d; j++) ss += row[j] * row[j];
        float norm = sqrtf(ss);
        if (norm > max_norm) max_norm = norm;
    }
    return max_norm < threshold;
}

static int inv4_mean_in_band(const float *batch, int total, float lo, float hi) {
    float s = 0.0f;
    for (int i = 0; i < total; i++) s += batch[i];
    float m = s / (float)total;
    return m >= lo && m <= hi;
}

static int inv5_variance_bounded(const float *batch, int total, float threshold) {
    float s = 0.0f;
    for (int i = 0; i < total; i++) s += batch[i];
    float m = s / (float)total;
    float v = 0.0f;
    for (int i = 0; i < total; i++) {
        float d = batch[i] - m;
        v += d * d;
    }
    v /= (float)total;
    return v < threshold;
}

static int inv6_max_bounded(const float *batch, int total, float threshold) {
    float maxabs = 0.0f;
    for (int i = 0; i < total; i++) {
        float v = batch[i];
        float a = v < 0.0f ? -v : v;
        if (a > maxabs) maxabs = a;
    }
    return maxabs < threshold;
}

static int inv7_row_sums_nonneg(const float *batch, int n, int d) {
    for (int i = 0; i < n; i++) {
        const float *row = batch + i * d;
        float s = 0.0f;
        for (int j = 0; j < d; j++) s += row[j];
        if (s < 0.0f) return 0;
    }
    return 1;
}

static int inv8_col_range_bounded(const float *batch, int n, int d, float threshold) {
    float worst = 0.0f;
    for (int j = 0; j < d; j++) {
        float lo = batch[j];
        float hi = batch[j];
        for (int i = 1; i < n; i++) {
            float v = batch[i * d + j];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        float r = hi - lo;
        if (r > worst) worst = r;
    }
    return worst < threshold;
}

static int inv9_determinism_fence(const float *batch, int n, int d) {
    int total = n * d;
    float total_sum = 0.0f;
    for (int i = 0; i < total; i++) total_sum += batch[i];

    float by_row = 0.0f;
    for (int i = 0; i < n; i++) {
        const float *row = batch + i * d;
        float rs = 0.0f;
        for (int j = 0; j < d; j++) rs += row[j];
        by_row += rs;
    }
    float scale = total_sum < 0.0f ? -total_sum : total_sum;
    if (scale < 1.0f) scale = 1.0f;
    float tol = (float)total * 1.1920929e-7f * scale;  /* f32 epsilon */
    float delta = total_sum - by_row;
    if (delta < 0.0f) delta = -delta;
    return delta <= tol;
}

/* ================================================================
 * EXPORT: single-invariant entry points
 * ================================================================ */

EXPORT int xrmgov_inv1_non_negative(const float *batch, int total) {
    kernel_guard();
    return inv1_non_negative(batch, total);
}

EXPORT int xrmgov_inv2_sum_bounded(const float *batch, int total, float threshold) {
    kernel_guard();
    return inv2_sum_bounded(batch, total, threshold);
}

EXPORT int xrmgov_inv3_l2_bounded(const float *batch, int n, int d, float threshold) {
    kernel_guard();
    return inv3_l2_bounded(batch, n, d, threshold);
}

EXPORT int xrmgov_inv4_mean_in_band(const float *batch, int total, float lo, float hi) {
    kernel_guard();
    return inv4_mean_in_band(batch, total, lo, hi);
}

EXPORT int xrmgov_inv5_variance_bounded(const float *batch, int total, float threshold) {
    kernel_guard();
    return inv5_variance_bounded(batch, total, threshold);
}

EXPORT int xrmgov_inv6_max_bounded(const float *batch, int total, float threshold) {
    kernel_guard();
    return inv6_max_bounded(batch, total, threshold);
}

EXPORT int xrmgov_inv7_row_sums_nonneg(const float *batch, int n, int d) {
    kernel_guard();
    return inv7_row_sums_nonneg(batch, n, d);
}

EXPORT int xrmgov_inv8_col_range_bounded(const float *batch, int n, int d, float threshold) {
    kernel_guard();
    return inv8_col_range_bounded(batch, n, d, threshold);
}

EXPORT int xrmgov_inv9_determinism_fence(const float *batch, int n, int d) {
    kernel_guard();
    return inv9_determinism_fence(batch, n, d);
}

/* ================================================================
 * EXPORT: combined gov9 — returns bitmask 0..0x1FF
 * ================================================================ */

EXPORT unsigned int xrmgov_gov9_evaluate(
    const float *batch, int n, int d,
    float sum_thresh, float l2_thresh,
    float mean_lo, float mean_hi,
    float var_thresh, float max_thresh, float col_range_thresh) {
    kernel_guard();
    unsigned int mask = 0;
    int total = n * d;
    if (inv1_non_negative(batch, total))                              mask |= 1u << 0;
    if (inv2_sum_bounded(batch, total, sum_thresh))                   mask |= 1u << 1;
    if (inv3_l2_bounded(batch, n, d, l2_thresh))                      mask |= 1u << 2;
    if (inv4_mean_in_band(batch, total, mean_lo, mean_hi))            mask |= 1u << 3;
    if (inv5_variance_bounded(batch, total, var_thresh))              mask |= 1u << 4;
    if (inv6_max_bounded(batch, total, max_thresh))                   mask |= 1u << 5;
    if (inv7_row_sums_nonneg(batch, n, d))                            mask |= 1u << 6;
    if (inv8_col_range_bounded(batch, n, d, col_range_thresh))        mask |= 1u << 7;
    if (inv9_determinism_fence(batch, n, d))                          mask |= 1u << 8;
    return mask;
}

/* ================================================================
 * EXPORT: protection queries
 * ================================================================ */

EXPORT int xrmgov_init(void)              { return xrmgov_protection_init(); }
EXPORT int xrmgov_check(void)             { return xrmgov_heartbeat(); }
EXPORT int xrmgov_protected(void)         { return xrmgov_is_protected(); }
EXPORT int xrmgov_verified(void)          { return xrmgov_auth_is_verified(); }
EXPORT const char *xrmgov_version(void)   { return xrmgov_get_version(); }
EXPORT void xrmgov_shutdown(void)         { xrmgov_shutdown_protection(); }
