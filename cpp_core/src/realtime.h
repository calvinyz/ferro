#pragma once

#include <cstdint>

struct RealtimeParams {
    int64_t period_ns;       // nominal loop period
    int64_t computation_ns;  // CPU time needed per period (macOS)
    int64_t constraint_ns;   // deadline for that computation (macOS)
    int fifo_priority;       // SCHED_FIFO priority, 1 to 99 (Linux)
};

// Requests real-time scheduling for the calling thread. Returns false if the
// request was refused, leaving the thread on default scheduling.
inline bool request_realtime(const RealtimeParams& p);

inline const char* realtime_backend();

#if defined(__APPLE__)

#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <pthread.h>

inline uint32_t ns_to_mach_abs(int64_t ns) {
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    return static_cast<uint32_t>(static_cast<double>(ns) * tb.denom / tb.numer);
}

inline bool request_realtime(const RealtimeParams& p) {
    thread_time_constraint_policy_data_t policy;
    policy.period = ns_to_mach_abs(p.period_ns);
    policy.computation = ns_to_mach_abs(p.computation_ns);
    policy.constraint = ns_to_mach_abs(p.constraint_ns);
    policy.preemptible = 0;

    kern_return_t ret = thread_policy_set(
        pthread_mach_thread_np(pthread_self()),
        THREAD_TIME_CONSTRAINT_POLICY,
        reinterpret_cast<thread_policy_t>(&policy),
        THREAD_TIME_CONSTRAINT_POLICY_COUNT);
    return ret == KERN_SUCCESS;
}

inline const char* realtime_backend() { return "mach THREAD_TIME_CONSTRAINT_POLICY"; }

#elif defined(__linux__)

#include <cstring>
#include <pthread.h>
#include <sched.h>

// period/computation/constraint are unused here; SCHED_FIFO runs the thread
// ahead of all normal-priority work until it blocks.
inline bool request_realtime(const RealtimeParams& p) {
    sched_param param;
    std::memset(&param, 0, sizeof(param));
    param.sched_priority = p.fifo_priority;
    return pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) == 0;
}

inline const char* realtime_backend() { return "SCHED_FIFO"; }

#else

inline bool request_realtime(const RealtimeParams&) { return false; }
inline const char* realtime_backend() { return "none"; }

#endif
