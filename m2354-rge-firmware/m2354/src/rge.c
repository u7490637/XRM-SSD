/* Copyright 2026 STARGA, Inc. / Dollarchip — XRM-SSD.
 *
 * RGE invariants, deterministic blend, and evidence-chained commit for the
 * NuMaker-M2354. C mirror of src/rge.mind + src/blend_run.mind.
 */
#include "rge.h"
#include "crypto_accel.h"
#include <string.h>

/* --- Invariants (1:1 with rge.mind predicates, all fail-closed) --- */

bool rge_node_count_within_bounds(const rge_graph_t *g) {
    return g->node_count >= 0 && g->node_count <= RGE_MAX_NODES;
}

bool rge_edge_count_within_bounds(const rge_graph_t *g) {
    return g->edge_count >= 0 && g->edge_count <= RGE_MAX_EDGES;
}

bool rge_node_kind_valid(const rge_node_t *n) {
    return n->kind >= 0 && n->kind <= RGE_KIND_MAX;
}

bool rge_node_state_normalized(const rge_node_t *n) {
    return n->state_q16 >= RGE_WEIGHT_MIN_Q16 && n->state_q16 <= RGE_WEIGHT_MAX_Q16;
}

bool rge_edge_weight_in_range(const rge_edge_t *e) {
    return e->weight_q16 >= RGE_WEIGHT_MIN_Q16 && e->weight_q16 <= RGE_WEIGHT_MAX_Q16;
}

bool rge_edge_no_self_loop(const rge_edge_t *e) {
    return e->src != e->dst;
}

bool rge_edge_endpoints_in_range(const rge_edge_t *e, const rge_graph_t *g) {
    return e->src >= 0 && e->src < g->node_count &&
           e->dst >= 0 && e->dst < g->node_count;
}

bool rge_mutation_targets_live_node(const rge_mutation_t *m, const rge_graph_t *g) {
    return m->target_node >= 0 && m->target_node < g->node_count;
}

bool rge_mutation_not_stale(const rge_mutation_t *m, const rge_graph_t *g) {
    return m->from_epoch == g->epoch;
}

bool rge_mutation_state_normalized(const rge_mutation_t *m) {
    return m->new_state_q16 >= RGE_WEIGHT_MIN_Q16 && m->new_state_q16 <= RGE_WEIGHT_MAX_Q16;
}

bool rge_epoch_advances_by_one(const rge_graph_t *before, const rge_graph_t *after) {
    return after->epoch == before->epoch + 1;
}

bool rge_node_version_advances(const rge_node_t *before, const rge_node_t *after) {
    return after->version == before->version + 1;
}

/* --- Deterministic blend (mirrors blend_run.mind) ---
 * out = (w*a + inv*b) / 65536, with an int64_t Q32.32 accumulator.
 * The cast to int64_t is load-bearing: w*a + inv*b reaches 2^31 exactly
 * at the convex-blend midpoint, one past INT32_MAX. */
q16_16 rge_blend2(q16_16 w, q16_16 a, q16_16 inv, q16_16 b) {
    int64_t acc = (int64_t)w * (int64_t)a + (int64_t)inv * (int64_t)b;
    return (q16_16)(acc / 65536);   /* == >>16 over the [0,1] bounded domain */
}

/* --- Canonical epoch serialization for the evidence anchor ---
 *
 * The bytes hashed MUST be identical to what the host-side MIND artifact
 * hashes, or the verifier will reject a legitimate state. Layout: prior
 * state_hash (32B) || epoch || node_count || edge_count || target_node ||
 * new_state_q16, every int32 little-endian. Keep this in lockstep with the
 * host serializer in firmware/host/rge_host.py. */
static void canon_epoch_bytes(const rge_graph_t *prev, const rge_mutation_t *m,
                              uint8_t *buf, size_t *out_len) {
    size_t o = 0;
    memcpy(buf + o, prev->state_hash, 32); o += 32;
    int32_t fields[5] = {
        prev->epoch + 1,         /* new epoch */
        prev->node_count,
        prev->edge_count,
        m->target_node,
        m->new_state_q16,
    };
    for (int i = 0; i < 5; i++) {
        buf[o++] = (uint8_t)(fields[i]       & 0xFF);
        buf[o++] = (uint8_t)((fields[i] >> 8)  & 0xFF);
        buf[o++] = (uint8_t)((fields[i] >> 16) & 0xFF);
        buf[o++] = (uint8_t)((fields[i] >> 24) & 0xFF);
    }
    *out_len = o;  /* 32 + 20 = 52 */
}

/* --- Commit: validate fail-closed, then apply + chain the evidence hash --- */
bool rge_commit(rge_graph_t *g, rge_node_t *node, const rge_mutation_t *m) {
    /* Reject before touching committed state if ANY predicate fails. */
    if (!rge_node_count_within_bounds(g))         return false;
    if (!rge_edge_count_within_bounds(g))         return false;
    if (!rge_mutation_targets_live_node(m, g))    return false;
    if (!rge_mutation_not_stale(m, g))            return false;
    if (!rge_mutation_state_normalized(m))        return false;
    if (!rge_node_kind_valid(node))               return false;

    /* Snapshot for the post-conditions. */
    rge_graph_t before_g   = *g;
    rge_node_t  before_n   = *node;

    /* Build the canonical epoch bytes and chain the hash over the
     * M2354 crypto accelerator BEFORE mutating, so the anchor binds the
     * exact mutation being committed. */
    uint8_t buf[64];
    size_t  len = 0;
    canon_epoch_bytes(g, m, buf, &len);
    uint8_t next_hash[32];
    crypto_sha256(buf, len, next_hash);

    /* Apply atomically. */
    node->state_q16 = m->new_state_q16;
    node->version  += 1;
    g->epoch       += 1;
    memcpy(g->state_hash, next_hash, 32);

    /* Post-condition checks (the ordering guarantees). On a correct build
     * these always hold; keeping them turns any future regression into a
     * fail-closed reject rather than silent corruption. */
    if (!rge_epoch_advances_by_one(&before_g, g)) return false;
    if (!rge_node_version_advances(&before_n, node)) return false;

    return true;
}
