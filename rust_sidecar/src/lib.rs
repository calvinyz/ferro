//! Sidecar consumer for the ferro control loop.

pub mod record;
pub mod stats;
pub mod trajectory;

use std::ffi::{CStr, c_char};

/// Ring size this crate was compiled against. The C++ side compares it to its
/// own sizeof to catch the two being built against different layout versions,
/// which the per-language static assertions cannot see.
#[unsafe(no_mangle)]
pub extern "C" fn ferro_sidecar_ring_size() -> u64 {
    size_of::<record::TickRing>() as u64
}

#[unsafe(no_mangle)]
pub extern "C" fn ferro_sidecar_stats_new() -> *mut stats::SidecarStats {
    Box::into_raw(Box::new(stats::SidecarStats::new()))
}

/// # Safety
/// `stats` must have come from `ferro_sidecar_stats_new` and not been freed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ferro_sidecar_stats_free(stats: *mut stats::SidecarStats) {
    if !stats.is_null() {
        drop(unsafe { Box::from_raw(stats) });
    }
}

/// # Safety
/// `stats` must be a live pointer from `ferro_sidecar_stats_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ferro_sidecar_stats_print(stats: *const stats::SidecarStats) {
    println!("{}", unsafe { &*stats });
}

/// # Safety
/// `stats` must be a live pointer from `ferro_sidecar_stats_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ferro_sidecar_stats_popped(stats: *const stats::SidecarStats) -> u64 {
    unsafe { (*stats).popped }
}

/// # Safety
/// `stats` must be a live pointer from `ferro_sidecar_stats_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ferro_sidecar_stats_dropped(stats: *const stats::SidecarStats) -> u64 {
    unsafe { (*stats).dropped }
}

/// Returns null if the file cannot be created.
///
/// # Safety
/// `path` must be a valid NUL-terminated string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ferro_sidecar_trajectory_open(
    path: *const c_char,
) -> *mut trajectory::Trajectory {
    let Ok(path) = unsafe { CStr::from_ptr(path) }.to_str() else {
        return std::ptr::null_mut();
    };
    match trajectory::Trajectory::create(path) {
        Ok(t) => Box::into_raw(Box::new(t)),
        Err(_) => std::ptr::null_mut(),
    }
}

/// Flushes and frees.
///
/// # Safety
/// `t` must have come from `ferro_sidecar_trajectory_open` and not been freed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ferro_sidecar_trajectory_close(t: *mut trajectory::Trajectory) {
    if !t.is_null() {
        let mut t = unsafe { Box::from_raw(t) };
        let _ = t.flush();
    }
}

/// Pops everything currently available, accumulating into `stats` and writing
/// each record to `traj` if it is non-null. Returns the number consumed.
///
/// # Safety
/// Same contract as `ferro_sidecar_pop`. `stats` must be live, and `traj`
/// either null or live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ferro_sidecar_drain_to(
    ring: *const record::TickRing,
    stats: *mut stats::SidecarStats,
    traj: *mut trajectory::Trajectory,
) -> u64 {
    let stats = unsafe { &mut *stats };
    let mut traj = unsafe { traj.as_mut() };
    let mut rec = record::TickRecord::default();
    let mut consumed = 0;

    while unsafe { ferro_sidecar_pop(ring, &mut rec) } {
        stats.record(&rec);
        if let Some(t) = traj.as_deref_mut() {
            let _ = t.write(&rec);
        }
        consumed += 1;
    }

    consumed
}

/// # Safety
/// Same contract as `ferro_sidecar_drain_to` with no trajectory.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ferro_sidecar_drain(
    ring: *const record::TickRing,
    stats: *mut stats::SidecarStats,
) -> u64 {
    unsafe { ferro_sidecar_drain_to(ring, stats, std::ptr::null_mut()) }
}

/// # Safety
/// `ring` must point to a valid, initialized TickRing that outlives this
/// call. Only ever call this from a single consumer thread.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ferro_sidecar_pop(
    ring: *const record::TickRing,
    out: *mut record::TickRecord,
) -> bool {
    // Raw pointer for slots: a &TickRing would assert no concurrent mutation.
    let head = unsafe { &(*ring).control.head };
    let tail = unsafe { &(*ring).control.tail };

    let t = tail.load(std::sync::atomic::Ordering::Relaxed);
    let h = head.load(std::sync::atomic::Ordering::Acquire);

    if t == h {
        return false;
    }

    let idx = (t & (record::RING_CAPACITY as u64 - 1)) as usize;
    unsafe { std::ptr::copy_nonoverlapping(&raw const (*ring).slots[idx], out, 1) };

    tail.store(t + 1, std::sync::atomic::Ordering::Release);
    true
}
