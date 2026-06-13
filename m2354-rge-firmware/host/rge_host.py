#!/usr/bin/env python3
# Copyright 2026 STARGA, Inc. / Dollarchip — XRM-SSD.
#
# Host-side reference of the RGE evidence chain. This is the "host" half of
# the two-tier hybrid: it produces the SAME canonical epoch bytes and the
# SAME SHA-256 state_hash the M2354 firmware (firmware/m2354/src/rge.c) does,
# so the device can attest "my recomputed hash == the host's hash."
#
# In the full MIND path this serialization is emitted by the MIND artifact;
# this Python mirror lets you verify byte-for-byte agreement today, before
# the Armv8-M codegen path exists.
import hashlib
import struct

WEIGHT_MIN, WEIGHT_MAX = 0, 65536
ROLLBACK_DEPTH = 3


def blend2(w, a, inv, b):
    """(w*a + inv*b) / 65536, Q32.32 intermediate (Python ints don't overflow)."""
    return (w * a + inv * b) // 65536


def canon_epoch_bytes(prev_hash, new_epoch, node_count, edge_count,
                      target_node, new_state_q16):
    """Must match canon_epoch_bytes() in rge.c exactly: 32B prev hash then
    five little-endian int32 fields."""
    buf = bytes(prev_hash)
    for f in (new_epoch, node_count, edge_count, target_node, new_state_q16):
        buf += struct.pack('<i', f)   # little-endian signed int32
    return buf


def commit(prev_hash, epoch, node_count, edge_count, target_node, new_state):
    # Fail-closed invariants (subset that the host can check).
    assert 0 <= node_count <= 50000
    assert 0 <= edge_count <= 400000
    assert 0 <= target_node < node_count
    assert WEIGHT_MIN <= new_state <= WEIGHT_MAX
    buf = canon_epoch_bytes(prev_hash, epoch + 1, node_count, edge_count,
                            target_node, new_state)
    return epoch + 1, hashlib.sha256(buf).digest()


def main():
    w, a, inv, b = 32768, 16384, 32768, 49152
    node_count, edge_count = 3, 2
    epoch = 0
    state_hash = bytes(32)  # genesis

    for _ in range(ROLLBACK_DEPTH):
        blended = blend2(w, a, inv, b)
        epoch, state_hash = commit(state_hash, epoch, node_count, edge_count,
                                   target_node=0, new_state=blended)
        print(f"epoch={epoch} state={blended} hash={state_hash.hex()}")

    print(f"\nFinal: epoch={epoch}  blended state should be 32768 -> {blend2(w,a,inv,b)}")


if __name__ == "__main__":
    main()
