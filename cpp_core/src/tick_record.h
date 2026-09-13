#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

// Shared memory layout between the C++ control loop (producer) and the Rust
// sidecar (consumer). Mirrored in rust_sidecar/src/record.rs; the static
// asserts at the bottom must match the ones there.

constexpr uint32_t kRingCapacity = 1024;  // power of two
constexpr uint32_t kObsDim = 4;           // matches InvertedPendulum

struct TickRecord {
    uint64_t tick;
    uint64_t timestamp_ns;
    float obs[kObsDim];
    float action;
    uint32_t inference_ns;
    uint32_t tick_work_ns;
    uint32_t _pad[5];
};

// head and tail sit on separate cache lines so the producer storing head does
// not invalidate the line the consumer is storing tail into.
struct RingControl {
    alignas(64) std::atomic<uint64_t> head;  // producer writes, consumer reads
    alignas(64) std::atomic<uint64_t> tail;  // consumer writes, producer reads
    alignas(64) uint32_t capacity;
    uint32_t record_size;
    uint32_t obs_dim;  // what the producer actually writes, for the consumer to check
};

struct TickRing {
    RingControl control;
    TickRecord slots[kRingCapacity];
};

static_assert(sizeof(TickRecord) == 64, "TickRecord must stay one cache line");
static_assert(offsetof(TickRecord, obs) == 16, "");
static_assert(offsetof(TickRecord, action) == 32, "");
static_assert(offsetof(TickRecord, inference_ns) == 36, "");
static_assert(sizeof(RingControl) == 192, "");
static_assert(offsetof(RingControl, tail) == 64, "");
static_assert(offsetof(RingControl, capacity) == 128, "");
static_assert(offsetof(RingControl, obs_dim) == 136, "");
static_assert(sizeof(TickRing) == 192 + 64 * kRingCapacity, "");
static_assert(std::atomic<uint64_t>::is_always_lock_free,
              "ring requires lock-free 64-bit atomics");

// The static asserts catch a one-sided layout edit but not a policy whose
// observation width differs from kObsDim. Call once at startup with the width
// the loaded ONNX model actually expects.
inline void validate_obs_dim(size_t actual) {
    if (actual != kObsDim) {
        throw std::runtime_error("policy observation width " + std::to_string(actual) +
                                 " does not match kObsDim " + std::to_string(kObsDim));
    }
}

// Producer side (this process). head/tail are unbounded counters, wrapped
// only when indexing into slots, so head == tail is unambiguously empty and
// head - tail == kRingCapacity is unambiguously full.
inline bool tick_ring_push(TickRing* ring, const TickRecord& rec) {
    uint64_t h = ring->control.head.load(std::memory_order_relaxed);
    uint64_t t = ring->control.tail.load(std::memory_order_acquire);

    if (h - t >= kRingCapacity) {
        return false;
    }

    uint32_t idx = h % kRingCapacity;
    ring->slots[idx] = rec;

    ring->control.head.store(h + 1, std::memory_order_release);
    return true;
}
