#include "tick_record.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

// Defined in rust_sidecar/src/lib.rs.
extern "C" uint64_t ferro_sidecar_ring_size();
extern "C" bool ferro_sidecar_pop(const TickRing* ring, TickRecord* out);

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
    return 0;
}
