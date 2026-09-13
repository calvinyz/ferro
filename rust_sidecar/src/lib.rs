//! Sidecar consumer for the ferro control loop.

pub mod record;

/// Ring size this crate was compiled against. The C++ side compares it to its
/// own sizeof to catch the two being built against different layout versions,
/// which the per-language static assertions cannot see.
#[unsafe(no_mangle)]
pub extern "C" fn ferro_sidecar_ring_size() -> u64 {
    size_of::<record::TickRing>() as u64
}
