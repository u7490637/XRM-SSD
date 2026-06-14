/* Copyright 2026 STARGA, Inc. / Dollarchip — XRM-SSD.
 *
 * NuMaker-M2354 demo: a governed RGE edge node.
 *
 * Each governed step:
 *   1. recompute a node's blended state from inbound edges (deterministic),
 *   2. submit it as an atomic, epoch-ordered mutation,
 *   3. on commit, chain the SHA-256 state_hash over the crypto accelerator,
 *   4. attest the new hash matches the value expected by the host/secure key.
 *
 * Build host-side first (make -C firmware host) to see it run on your laptop;
 * flash with `make M2354_BSP=1` once the Nuvoton BSP paths are wired.
 */
#include "rge.h"
#include "crypto_accel.h"
#include <stdio.h>
#include <string.h>

/* Output works on BOTH targets: host -> stdout; device -> semihosting (the SWD
 * debugger console), so seeing the run on silicon needs no UART pin-mux or
 * clock bring-up — the riskiest board-specific bits are sidestepped. */
static void print_hash(const char *label, const uint8_t h[32]) {
    printf("%s ", label);
    for (int i = 0; i < 32; i++) printf("%02x", h[i]);
    printf("\n");
}

int main(void) {
#ifndef HOST_BUILD
    /* Route newlib stdio over the semihosting channel to the debugger console. */
    extern void initialise_monitor_handles(void);
    initialise_monitor_handles();
#endif
    crypto_accel_init();

    /* A tiny graph: one COMPUTE node, genesis epoch, zero hash. */
    rge_graph_t g = { .node_count = 3, .edge_count = 2, .epoch = 0 };
    memset(g.state_hash, 0, 32);   /* genesis anchor */
    rge_node_t node = { .kind = RGE_KIND_COMPUTE, .state_q16 = 0, .version = 0 };

    /* Two inbound edge weights + neighbor states (Q16.16 raw), the same
     * values blend_run.mind evaluates: 0.5*0.25 + 0.5*0.75 = 0.5 -> 32768. */
    q16_16 w = 32768, a = 16384, inv = 32768, b = 49152;

    for (int step = 0; step < RGE_ROLLBACK_DEPTH; step++) {
        q16_16 blended = rge_blend2(w, a, inv, b);

        rge_mutation_t m = {
            .target_node  = 0,
            .from_epoch   = g.epoch,
            .new_state_q16 = blended,
        };

        if (rge_commit(&g, &node, &m)) {
            printf("epoch=%d node.state=%d node.version=%d  ",
                   (int)g.epoch, (int)node.state_q16, (int)node.version);
            print_hash("hash=", g.state_hash);
        } else {
            printf("epoch=%d  MUTATION REJECTED (fail-closed)\n", (int)g.epoch);
        }
    }

    printf("\nFinal: epoch=%d  blended state should be 32768 -> %d\n",
           (int)g.epoch, (int)node.state_q16);
    return 0;
}
