#pragma once

#include "tick_record.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

// Defined in rust_sidecar/src/lib.rs. SidecarStats stays opaque here so
// TickRing remains the only type needing a two-sided layout contract.
struct SidecarStats;
extern "C" SidecarStats* ferro_sidecar_stats_new();
extern "C" void ferro_sidecar_stats_free(SidecarStats* stats);
extern "C" void ferro_sidecar_stats_print(const SidecarStats* stats);
extern "C" uint64_t ferro_sidecar_stats_popped(const SidecarStats* stats);
extern "C" uint64_t ferro_sidecar_stats_dropped(const SidecarStats* stats);
extern "C" uint64_t ferro_sidecar_drain(const TickRing* ring, SidecarStats* stats);

// Drains the ring from one background thread, which is what the SPSC contract
// allows. Without a consumer running the ring is just a buffer that fills.
class SidecarConsumer {
    const TickRing* ring;
    SidecarStats* stats;
    std::atomic<bool> running;
    std::chrono::microseconds poll_interval;
    std::thread worker;

public:
    explicit SidecarConsumer(const TickRing* ring,
                             std::chrono::microseconds poll = std::chrono::microseconds(1000))
        : ring(ring),
          stats(ferro_sidecar_stats_new()),
          running(true),
          poll_interval(poll) {
        worker = std::thread([this] {
            // std::thread inherits the creator's scheduling policy, and the
            // control thread may be SCHED_FIFO by now. Telemetry must not
            // compete with control, so drop back to normal scheduling.
#if defined(__linux__)
            sched_param normal{};
            normal.sched_priority = 0;
            pthread_setschedparam(pthread_self(), SCHED_OTHER, &normal);
#endif

            while (running.load(std::memory_order_relaxed)) {
                ferro_sidecar_drain(this->ring, stats);
                std::this_thread::sleep_for(poll_interval);
            }
            // Catch anything pushed between the last poll and the stop.
            ferro_sidecar_drain(this->ring, stats);
        });
    }

    ~SidecarConsumer() {
        stop();
        ferro_sidecar_stats_free(stats);
    }

    SidecarConsumer(const SidecarConsumer&) = delete;
    SidecarConsumer& operator=(const SidecarConsumer&) = delete;

    // Idempotent so the destructor is safe after an explicit stop.
    void stop() {
        if (worker.joinable()) {
            running.store(false, std::memory_order_relaxed);
            worker.join();
        }
    }

    uint64_t popped() const { return ferro_sidecar_stats_popped(stats); }
    uint64_t dropped() const { return ferro_sidecar_stats_dropped(stats); }

    void print_summary() const {
        // Rust buffers stdout separately from C, so flush or the two interleave.
        std::fflush(stdout);
        ferro_sidecar_stats_print(stats);
    }
};
