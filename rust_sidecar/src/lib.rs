//! Sidecar consumer for the ferro control loop.

pub mod record;

/// Ring size this crate was compiled against. The C++ side compares it to its
/// own sizeof to catch the two being built against different layout versions,
/// which the per-language static assertions cannot see.
#[unsafe(no_mangle)]
pub extern "C" fn ferro_sidecar_ring_size() -> u64 {
    size_of::<record::TickRing>() as u64
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
