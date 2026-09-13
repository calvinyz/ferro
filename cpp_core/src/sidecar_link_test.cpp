#include "tick_record.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

// Defined in rust_sidecar/src/lib.rs.
struct SidecarStats;
extern "C" uint64_t ferro_sidecar_ring_size();
extern "C" bool ferro_sidecar_pop(const TickRing* ring, TickRecord* out);
extern "C" SidecarStats* ferro_sidecar_stats_new();
extern "C" void ferro_sidecar_stats_free(SidecarStats* stats);
extern "C" void ferro_sidecar_stats_print(const SidecarStats* stats);
extern "C" uint64_t ferro_sidecar_stats_popped(const SidecarStats* stats);
extern "C" uint64_t ferro_sidecar_stats_dropped(const SidecarStats* stats);
extern "C" uint64_t ferro_sidecar_drain(const TickRing* ring, SidecarStats* stats);

int main() {
    uint64_t rust_size = ferro_sidecar_ring_size();
    std::printf("C++  TickRing: %zu bytes\n", sizeof(TickRing));
    std::printf("Rust TickRing: %llu bytes\n", static_cast<unsigned long long>(rust_size));

    if (rust_size != sizeof(TickRing)) {
        std::printf("MISMATCH: the two sides were built against different layouts\n");
        return 1;
    }
    std::printf("layouts agree\n");

    // Large (~65KB); keep it off the stack.
    static TickRing ring{};
    ring.control.head.store(0, std::memory_order_relaxed);
    ring.control.tail.store(0, std::memory_order_relaxed);
    ring.control.capacity = kRingCapacity;
    ring.control.record_size = sizeof(TickRecord);
    ring.control.obs_dim = kObsDim;

    TickRecord sent{};
    sent.tick = 42;
    sent.timestamp_ns = 123456789;
    sent.obs[0] = 1.0f;
    sent.obs[1] = 2.0f;
    sent.obs[2] = 3.0f;
    sent.obs[3] = 4.0f;
    sent.action = 0.5f;
    sent.inference_ns = 100;
    sent.tick_work_ns = 200;

    if (!tick_ring_push(&ring, sent)) {
        std::printf("push failed (ring reported full on an empty ring)\n");
        return 1;
    }
    std::printf("pushed tick %llu\n", static_cast<unsigned long long>(sent.tick));

    TickRecord received{};
    if (!ferro_sidecar_pop(&ring, &received)) {
        std::printf("pop failed (ring reported empty right after a push)\n");
        return 1;
    }

    if (std::memcmp(&sent, &received, sizeof(TickRecord)) != 0) {
        std::printf("MISMATCH: received record differs from what was sent\n");
        return 1;
    }
    std::printf("popped tick %llu, obs=[%.1f, %.1f, %.1f, %.1f], action=%.1f\n",
                static_cast<unsigned long long>(received.tick),
                received.obs[0], received.obs[1], received.obs[2], received.obs[3],
                received.action);

    // Ring should be empty again now.
    TickRecord unused{};
    if (ferro_sidecar_pop(&ring, &unused)) {
        std::printf("MISMATCH: pop succeeded on an empty ring\n");
        return 1;
    }

    std::printf("round trip OK\n");

    // Drain with a deliberate hole in the tick sequence. The consumer infers
    // producer drops from the gap rather than from a shared counter.
    const uint64_t ticks[] = {0, 1, 2, 7};
    for (uint64_t t : ticks) {
        TickRecord r{};
        r.tick = t;
        r.inference_ns = 100;
        r.tick_work_ns = 200;
        if (!tick_ring_push(&ring, r)) {
            std::printf("push failed on tick %llu\n", static_cast<unsigned long long>(t));
            return 1;
        }
    }

    SidecarStats* stats = ferro_sidecar_stats_new();
    uint64_t consumed = ferro_sidecar_drain(&ring, stats);

    // Rust's stdout buffers separately from C's, so flush before handing the
    // terminal to the other side or the two interleave out of order.
    std::fflush(stdout);
    ferro_sidecar_stats_print(stats);

    uint64_t popped = ferro_sidecar_stats_popped(stats);
    uint64_t dropped = ferro_sidecar_stats_dropped(stats);
    ferro_sidecar_stats_free(stats);

    if (consumed != 4 || popped != 4) {
        std::printf("MISMATCH: expected 4 records, drained %llu / counted %llu\n",
                    static_cast<unsigned long long>(consumed),
                    static_cast<unsigned long long>(popped));
        return 1;
    }

    // Ticks 3 through 6 never arrived.
    if (dropped != 4) {
        std::printf("MISMATCH: expected 4 inferred drops, got %llu\n",
                    static_cast<unsigned long long>(dropped));
        return 1;
    }

    std::printf("drain + drop inference OK\n");
    return 0;
}
