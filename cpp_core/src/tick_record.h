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
constexpr uint32_t kMaxQposDim = 16;
constexpr uint32_t kMaxActionDim = 8;

// Simulator state and command per tick, enough to replay or render a run.
// Two cache lines.
struct TickRecord {
    uint64_t tick;
    uint64_t timestamp_ns;
    uint32_t inference_ns;
    uint32_t tick_work_ns;
    uint16_t qpos_dim;
    uint16_t action_dim;
    uint32_t _pad;
    float qpos[kMaxQposDim];
    float action[kMaxActionDim];
};

// head and tail sit on separate cache lines so the producer storing head does
// not invalidate the line the consumer is storing tail into.
struct RingControl {
    alignas(64) std::atomic<uint64_t> head;  // producer writes, consumer reads
    alignas(64) std::atomic<uint64_t> tail;  // consumer writes, producer reads
    alignas(64) uint32_t capacity;
    uint32_t record_size;
    uint32_t qpos_capacity;
};

struct TickRing {
    RingControl control;
    TickRecord slots[kRingCapacity];
};

static_assert(sizeof(TickRecord) == 128, "TickRecord must stay two cache lines");
static_assert(offsetof(TickRecord, inference_ns) == 16, "");
static_assert(offsetof(TickRecord, qpos_dim) == 24, "");
static_assert(offsetof(TickRecord, qpos) == 32, "");
static_assert(offsetof(TickRecord, action) == 96, "");
static_assert(sizeof(RingControl) == 192, "");
static_assert(offsetof(RingControl, tail) == 64, "");
static_assert(offsetof(RingControl, capacity) == 128, "");
static_assert(offsetof(RingControl, qpos_capacity) == 136, "");
static_assert(sizeof(TickRing) == 192 + 128 * kRingCapacity, "");
static_assert(std::atomic<uint64_t>::is_always_lock_free,
              "ring requires lock-free 64-bit atomics");

// Static asserts fix the record size but cannot see the loaded model, so check
// once at startup that its state and command fit.
inline void validate_record_dims(int nq, int nu) {
    if (nq > static_cast<int>(kMaxQposDim) || nu > static_cast<int>(kMaxActionDim)) {
        throw std::runtime_error("model nq=" + std::to_string(nq) + " nu=" + std::to_string(nu) +
                                 " exceeds record capacity " + std::to_string(kMaxQposDim) + "/" +
                                 std::to_string(kMaxActionDim));
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
