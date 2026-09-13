//! Mirror of cpp_core/src/tick_record.h. The assertions below must match the
//! static_asserts there.

use std::sync::atomic::AtomicU64;

pub const RING_CAPACITY: usize = 1024;
pub const OBS_DIM: usize = 4;

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct TickRecord {
    pub tick: u64,
    pub timestamp_ns: u64,
    pub obs: [f32; OBS_DIM],
    pub action: f32,
    pub inference_ns: u32,
    pub tick_work_ns: u32,
    pub _pad: [u32; 5],
}

/// head and tail are padded onto separate cache lines to avoid false sharing
/// between producer and consumer.
#[repr(C, align(64))]
pub struct RingControl {
    pub head: AtomicU64,
    _pad0: [u8; 56],
    pub tail: AtomicU64,
    _pad1: [u8; 56],
    pub capacity: u32,
    pub record_size: u32,
    /// What the producer actually writes, for the consumer to check.
    pub obs_dim: u32,
    _pad2: [u8; 52],
}

#[repr(C)]
pub struct TickRing {
    pub control: RingControl,
    pub slots: [TickRecord; RING_CAPACITY],
}

const _: () = assert!(std::mem::size_of::<TickRecord>() == 64);
const _: () = assert!(std::mem::offset_of!(TickRecord, obs) == 16);
const _: () = assert!(std::mem::offset_of!(TickRecord, action) == 32);
const _: () = assert!(std::mem::offset_of!(TickRecord, inference_ns) == 36);
const _: () = assert!(std::mem::size_of::<RingControl>() == 192);
const _: () = assert!(std::mem::offset_of!(RingControl, tail) == 64);
const _: () = assert!(std::mem::offset_of!(RingControl, capacity) == 128);
const _: () = assert!(std::mem::offset_of!(RingControl, obs_dim) == 136);
const _: () = assert!(std::mem::size_of::<TickRing>() == 192 + 64 * RING_CAPACITY);
