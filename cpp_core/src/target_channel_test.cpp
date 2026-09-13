#include "target_channel.h"

#include <atomic>
#include <cstdio>
#include <thread>

// Every field is derived from policy_tick, so a mix of old and new values is
// detectable as a torn read.
static Target make_target(uint64_t tick) {
    Target t{};
    t.policy_tick = tick;
    t.published_ns = tick * 1000;
    for (uint32_t i = 0; i < kMaxTargetDim; i++) {
        t.value[i] = static_cast<float>(tick + i);
    }
    return t;
}

static bool consistent(const Target& t) {
    if (t.published_ns != t.policy_tick * 1000) return false;
    for (uint32_t i = 0; i < kMaxTargetDim; i++) {
        if (t.value[i] != static_cast<float>(t.policy_tick + i)) return false;
    }
    return true;
}

int main() {
    constexpr uint64_t kWrites = 2'000'000;

    TargetChannel channel;
    std::atomic<bool> done{false};

    std::thread writer([&] {
        for (uint64_t i = 1; i <= kWrites; i++) {
            channel.publish(make_target(i));
        }
        done.store(true, std::memory_order_release);
    });

    uint64_t reads = 0, retries = 0, torn = 0, went_backwards = 0;
    uint64_t last_tick = 0;

    while (!done.load(std::memory_order_acquire)) {
        Target t{};
        if (!channel.try_read(t)) {
            retries++;
            continue;
        }
        // tick 0 is the initial state, before the first publish
        if (t.policy_tick == 0) continue;
        reads++;

        if (!consistent(t)) torn++;
        if (t.policy_tick < last_tick) went_backwards++;
        last_tick = t.policy_tick;
    }

    writer.join();

    std::printf("writes        : %llu\n", (unsigned long long)kWrites);
    std::printf("reads         : %llu\n", (unsigned long long)reads);
    std::printf("write retries : %llu\n", (unsigned long long)retries);
    std::printf("torn reads    : %llu\n", (unsigned long long)torn);
    std::printf("went backwards: %llu\n", (unsigned long long)went_backwards);

    if (torn || went_backwards) {
        std::printf("FAIL\n");
        return 1;
    }
    if (retries == 0) {
        std::printf("FAIL: no contention observed, test proves nothing\n");
        return 1;
    }

    std::printf("PASS\n");
    return 0;
}
