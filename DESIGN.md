# Design

## Threads

The policy thread does not read `mjData`, because the control loop is mutating
it and a mutex would let a slow policy stall the control loop, so state goes out
through a channel instead. Neither channel ever blocks its reader.

## Ring

Push drops when full rather than overwriting or blocking, so a full ring costs
the producer a compare and a return.

Head and tail are unbounded counters, wrapped only when indexing, so
`head == tail` is empty and `head - tail == capacity` is full, giving a capacity
of 1024 with no sacrificial slot. Running 1500 ticks with no consumer attached
drops exactly 476 records, which confirms the accounting.

`alignas(64)` puts head and tail on separate cache lines, and `TickRecord` is
asserted at 64 bytes.

Release on the head store publishes the slot and acquire on the tail load
confirms the consumer is done with it, so head ordering keeps the consumer from
reading an unwritten slot while tail ordering keeps the producer from
overwriting a slot still being read.

Drops are inferred from gaps in the tick sequence rather than a shared counter.

## Cross-language layout

`cpp_core/src/tick_record.h` and `rust_sidecar/src/record.rs` describe the same
bytes, both with static assertions on struct size and field offsets, and
`ferro_sidecar_ring_size()` compares `sizeof(TickRing)` at runtime to catch the
two sides being built against different layout versions. Static assertions do
not cover a policy whose observation width differs from `kObsDim`, so
`validate_obs_dim` checks the loaded ONNX model at startup.

The end-to-end check is that producer and consumer report the same latency
statistics over 1500 records, one computed in C++ from local variables and one
in Rust from records that crossed the FFI boundary.

## Memory ordering

The ring stress test passes with every ordering downgraded to relaxed, for two
reasons:

- x86-TSO forbids the StoreStore reordering that would break the ring, so
  relaxed emits the same instruction.
- Relaxed also permits the compiler to sink the slot store below the head store
  on any target.

So the test covers ring logic rather than ordering, and ThreadSanitizer or a
model checker would be needed for the latter. The seqlock test at least runs on
ARM, which is weakly ordered.

The seqlock reads its payload while the writer may be writing it, which is
formally UB, and a conforming version would make each field atomic and read them
relaxed.

## Scheduling

`realtime.h` requests mach `THREAD_TIME_CONSTRAINT_POLICY` on macOS and
`SCHED_FIFO` on Linux, falling back to default priority if refused.

`std::thread` inherits the creating thread's scheduling policy, so the telemetry
consumer became `SCHED_FIFO` 80 once the control thread got real-time, and it
now sets itself to `SCHED_OTHER`.

## Stale commands

Commands carry their publish time, and the control loop reads the latest one
each tick and computes its age, decaying the held action toward zero once it is
older than three policy periods. Killing the policy thread mid-run gives 3851
fallback ticks, a worst command age of 3.97s, and zero missed deadlines.

## Dependencies

On macOS the MuJoCo dylib is lifted out of its framework, its install name
rewritten, and then re-signed ad-hoc, since arm64 macOS will not load an
unsigned dylib.

ONNX Runtime's own CMake config points at `include/onnxruntime` and `lib64/`,
which are not in its tarball, so the imported target is declared directly.
