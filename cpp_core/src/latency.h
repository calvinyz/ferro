#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

struct LatencyStats {
    size_t count;
    double mean_us;
    double p50_us;
    double p99_us;
    double min_us;
    double max_us;
};

// Reserves capacity upfront so record() never allocates on the measured path.
class LatencyRecorder {
    std::vector<int64_t> samples;

public:
    explicit LatencyRecorder(size_t capacity) { samples.reserve(capacity); }

    void record(std::chrono::steady_clock::duration elapsed) {
        samples.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
    }

    size_t count() const { return samples.size(); }

    // Sorts samples, so call this after the loop rather than inside it.
    LatencyStats stats() {
        LatencyStats s = {};
        s.count = samples.size();
        if (samples.empty()) return s;

        std::sort(samples.begin(), samples.end());

        int64_t total = 0;
        for (int64_t v : samples) total += v;

        s.mean_us = static_cast<double>(total) / samples.size() / 1000.0;
        s.p50_us = percentile_us(50.0);
        s.p99_us = percentile_us(99.0);
        s.min_us = samples.front() / 1000.0;
        s.max_us = samples.back() / 1000.0;
        return s;
    }

private:
    // Nearest-rank, assumes samples are already sorted.
    double percentile_us(double p) const {
        size_t rank = static_cast<size_t>(std::ceil(p / 100.0 * samples.size()));
        if (rank == 0) rank = 1;
        if (rank > samples.size()) rank = samples.size();
        return samples[rank - 1] / 1000.0;
    }
};

inline void print_latency(const char* label, const LatencyStats& s) {
    std::printf("%-16s n=%-6zu mean=%7.1fus p50=%7.1fus p99=%7.1fus min=%7.1fus max=%7.1fus\n",
                label, s.count, s.mean_us, s.p50_us, s.p99_us, s.min_us, s.max_us);
}
