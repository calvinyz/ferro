#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

// Latest-value handoff, policy thread to control loop. Not a queue: the loop
// wants the newest command, and a FIFO would deliver stale ones after a stall.
// Readers never block, so a slow policy cannot stretch a control tick.

constexpr uint32_t kMaxTargetDim = 8;

struct Target {
    uint64_t policy_tick;
    uint64_t published_ns;
    float value[kMaxTargetDim];
};

class TargetChannel {
    std::atomic<uint64_t> seq;  // odd means a write is in flight
    Target payload;

public:
    TargetChannel() : seq(0), payload{} {}

    TargetChannel(const TargetChannel&) = delete;
    TargetChannel& operator=(const TargetChannel&) = delete;

    // Single writer.
    void publish(const Target& t) {
        uint64_t s = seq.load(std::memory_order_relaxed);

        // acq_rel so the payload writes cannot be hoisted above this bump
        seq.store(s + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acq_rel);

        std::memcpy(&payload, &t, sizeof(Target));

        std::atomic_thread_fence(std::memory_order_release);
        seq.store(s + 2, std::memory_order_relaxed);
    }

    // False if a write was in flight; caller keeps its previous target.
    bool try_read(Target& out) const {
        uint64_t before = seq.load(std::memory_order_acquire);
        if (before & 1) return false;

        std::memcpy(&out, &payload, sizeof(Target));

        std::atomic_thread_fence(std::memory_order_acquire);
        return before == seq.load(std::memory_order_acquire);
    }

    uint64_t publications() const { return seq.load(std::memory_order_relaxed) / 2; }
};
