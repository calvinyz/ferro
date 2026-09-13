// Two-thread stress test for the SPSC ring: C++ producer, Rust consumer.

#include "tick_record.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

extern "C" bool ferro_sidecar_pop(const TickRing* ring, TickRecord* out);

namespace {

constexpr uint64_t kTotal = 2'000'000;

// Every field derives from tick, so a torn record fails the comparison.
TickRecord make_record(uint64_t tick) {
    TickRecord r{};
    r.tick = tick;
    r.timestamp_ns = tick * 1000;
    for (uint32_t i = 0; i < kObsDim; ++i) {
        r.obs[i] = static_cast<float>(tick % 1000) + static_cast<float>(i);
    }
    r.action = static_cast<float>(tick % 997);
    r.inference_ns = static_cast<uint32_t>(tick % 100000);
    r.tick_work_ns = static_cast<uint32_t>(tick % 50000);
    return r;
}

}  // namespace

int main() {
    static TickRing ring{};
    ring.control.capacity = kRingCapacity;
    ring.control.record_size = sizeof(TickRecord);
    ring.control.obs_dim = kObsDim;

    std::atomic<uint64_t> full_retries{0};
    auto start = std::chrono::steady_clock::now();

    // Retry instead of drop, so the consumer must see every tick once, in order.
    std::thread producer([&] {
        for (uint64_t tick = 0; tick < kTotal; ++tick) {
            TickRecord r = make_record(tick);
            while (!tick_ring_push(&ring, r)) {
                full_retries.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::yield();
            }
        }
    });

    uint64_t expected = 0;
    uint64_t torn = 0;
    uint64_t out_of_order = 0;
    uint64_t empty_polls = 0;
    TickRecord got{};

    while (expected < kTotal) {
        if (!ferro_sidecar_pop(&ring, &got)) {
            ++empty_polls;
            std::this_thread::yield();
            continue;
        }
        if (got.tick != expected) {
            ++out_of_order;
        }
        TickRecord want = make_record(got.tick);
        if (std::memcmp(&got, &want, sizeof(TickRecord)) != 0) {
            ++torn;
        }
        ++expected;
    }

    producer.join();
    auto elapsed = std::chrono::steady_clock::now() - start;
    double secs = std::chrono::duration<double>(elapsed).count();

    std::printf("records      : %llu\n", static_cast<unsigned long long>(kTotal));
    std::printf("elapsed      : %.2f s (%.2f M records/s)\n", secs, kTotal / secs / 1e6);
    std::printf("full retries : %llu\n",
                static_cast<unsigned long long>(full_retries.load()));
    std::printf("empty polls  : %llu\n", static_cast<unsigned long long>(empty_polls));
    std::printf("out of order : %llu\n", static_cast<unsigned long long>(out_of_order));
    std::printf("torn records : %llu\n", static_cast<unsigned long long>(torn));

    if (out_of_order != 0 || torn != 0) {
        std::printf("FAIL\n");
        return 1;
    }
    std::printf("PASS\n");
    return 0;
}
