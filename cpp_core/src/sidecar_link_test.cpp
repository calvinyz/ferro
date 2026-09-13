#include "tick_record.h"

#include <cstdint>
#include <cstdio>

// Defined in rust_sidecar/src/lib.rs.
extern "C" uint64_t ferro_sidecar_ring_size();

int main() {
    uint64_t rust_size = ferro_sidecar_ring_size();
    std::printf("C++  TickRing: %zu bytes\n", sizeof(TickRing));
    std::printf("Rust TickRing: %llu bytes\n", static_cast<unsigned long long>(rust_size));

    if (rust_size != sizeof(TickRing)) {
        std::printf("MISMATCH: the two sides were built against different layouts\n");
        return 1;
    }
    std::printf("layouts agree\n");
    return 0;
}
