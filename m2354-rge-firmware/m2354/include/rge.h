/* Copyright 2026 STARGA, Inc. / Dollarchip — XRM-SSD.
 *
 * RGE state model + invariants for the NuMaker-M2354 (Cortex-M23, Armv8-M).
 *
 * This header is the C mirror of src/rge.mind. Every struct field, bound,
 * node kind, and invariant predicate below corresponds 1:1 to the MIND
 * VERIFY-ONLY contract, so that the on-device verifier and the host-side
 * MIND artifact agree on what a valid graph/mutation is — byte for byte.
 *
 * All node/edge magnitudes are Q16.16 fixed-point (raw = value << 16),
 * the same representation MIND uses for cross-substrate byte-identity.
 */
#ifndef XRM_RGE_H
#define XRM_RGE_H

#include <stdint.h>
#include <stdbool.h>

/* Q16.16 fixed point. raw = value * 65536. */
typedef int32_t q16_16;

/* --- Bounds (Design Book Ch.5.1: 50k nodes on FPGA, <0.5 ms mutate) --- */
#define RGE_MAX_NODES      50000
#define RGE_MAX_EDGES      400000   /* ~8 fan-out average */

/* Edge weights / node state normalized to [0.0, 1.0] in Q16.16. */
#define RGE_WEIGHT_MIN_Q16 0        /* 0.0 */
#define RGE_WEIGHT_MAX_Q16 65536    /* 1.0 */

/* Rollback / evidence window: last 3 consistent snapshots (firmware OTA
 * AND state-graph rollback share this depth — confirmed by Dollarchip). */
#define RGE_ROLLBACK_DEPTH 3

/* --- Node kinds: every hardware subsystem is a typed node --- */
#define RGE_KIND_AESA        0
#define RGE_KIND_COMPUTE     1
#define RGE_KIND_ISL         2
#define RGE_KIND_THERMAL     3
#define RGE_KIND_POWER       4
#define RGE_KIND_DTN         5
#define RGE_KIND_SEMANTIC    6   /* XRM-II: semantic-streaming node */
#define RGE_KIND_CONJUNCTION 7   /* XRM-II: conjunction-survival controller */
#define RGE_KIND_MAX         7   /* extensible above this via Runtime Graph OTA */

/* --- State model (mirrors rge.mind structs) --- */

typedef struct {
    int32_t kind;
    q16_16  state_q16;   /* subsystem scalar state, normalized */
    int32_t version;     /* bumped on every accepted mutation */
} rge_node_t;

typedef struct {
    int32_t src;
    int32_t dst;
    q16_16  weight_q16;
} rge_edge_t;

/* Graph header carries the evidence anchor. state_hash chains the prior
 * epoch's hash with the applied mutation (mirrors MIND's trace_hash
 * provenance shape). On-device it is a full SHA-256 digest (see rge_hash). */
typedef struct {
    int32_t  node_count;
    int32_t  edge_count;
    int32_t  epoch;
    uint8_t  state_hash[32];   /* SHA-256 of canonical epoch bytes */
} rge_graph_t;

/* A single atomic mutation request against one node. */
typedef struct {
    int32_t target_node;
    int32_t from_epoch;
    q16_16  new_state_q16;
} rge_mutation_t;

/* --- Invariants: fail-closed predicates (mirror rge.mind) --- */

bool rge_node_count_within_bounds(const rge_graph_t *g);
bool rge_edge_count_within_bounds(const rge_graph_t *g);
bool rge_node_kind_valid(const rge_node_t *n);
bool rge_node_state_normalized(const rge_node_t *n);
bool rge_edge_weight_in_range(const rge_edge_t *e);
bool rge_edge_no_self_loop(const rge_edge_t *e);
bool rge_edge_endpoints_in_range(const rge_edge_t *e, const rge_graph_t *g);

bool rge_mutation_targets_live_node(const rge_mutation_t *m, const rge_graph_t *g);
bool rge_mutation_not_stale(const rge_mutation_t *m, const rge_graph_t *g);
bool rge_mutation_state_normalized(const rge_mutation_t *m);
bool rge_epoch_advances_by_one(const rge_graph_t *before, const rge_graph_t *after);
bool rge_node_version_advances(const rge_node_t *before, const rge_node_t *after);

/* --- Deterministic blend (mirrors blend_run.mind) ---
 *
 * Pull-model recompute of one node folded against two inbound edges:
 *   out = (w*a + inv*b) / 65536
 * The product of two Q16.16 values is Q32.32 — the accumulator MUST be
 * int64_t or it overflows at exactly 2^31 (the empirically-confirmed
 * blend_run.mind gotcha). Rescale is integer divide by 2^16, which agrees
 * with >>16 over this non-negative bounded domain. */
q16_16 rge_blend2(q16_16 w, q16_16 a, q16_16 inv, q16_16 b);

/* --- Commit + evidence ---
 *
 * Validate a mutation against the current graph (all fail-closed predicates),
 * and if valid, apply it: bump the node, advance the epoch by one, and chain
 * the new state_hash over the M2354 crypto accelerator. Returns true on
 * commit, false (graph untouched) on any invariant violation. */
bool rge_commit(rge_graph_t *g, rge_node_t *node, const rge_mutation_t *m);

#endif /* XRM_RGE_H */
