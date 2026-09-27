//! Mirror of cpp_core/src/tick_record.h. The assertions below must match the
//! static_asserts there.

use std::sync::atomic::AtomicU64;

pub const RING_CAPACITY: usize = 1024;
pub const MAX_QPOS_DIM: usize = 16;
pub const MAX_ACTION_DIM: usize = 8;

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct TickRecord {
    pub tick: u64,
    pub timestamp_ns: u64,
    pub inference_ns: u32,
    pub tick_work_ns: u32,
    pub qpos_dim: u16,
    pub action_dim: u16,
    pub _pad: u32,
    pub qpos: [f32; MAX_QPOS_DIM],
    pub action: [f32; MAX_ACTION_DIM],
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
    pub qpos_capacity: u32,
    _pad2: [u8; 52],
}

#[repr(C)]
pub struct TickRing {
    pub control: RingControl,
    pub slots: [TickRecord; RING_CAPACITY],
}

const _: () = assert!(std::mem::size_of::<TickRecord>() == 128);
const _: () = assert!(std::mem::offset_of!(TickRecord, inference_ns) == 16);
const _: () = assert!(std::mem::offset_of!(TickRecord, qpos_dim) == 24);
const _: () = assert!(std::mem::offset_of!(TickRecord, qpos) == 32);
const _: () = assert!(std::mem::offset_of!(TickRecord, action) == 96);
const _: () = assert!(std::mem::size_of::<RingControl>() == 192);
const _: () = assert!(std::mem::offset_of!(RingControl, tail) == 64);
const _: () = assert!(std::mem::offset_of!(RingControl, capacity) == 128);
const _: () = assert!(std::mem::offset_of!(RingControl, qpos_capacity) == 136);
const _: () = assert!(std::mem::size_of::<TickRing>() == 192 + 128 * RING_CAPACITY);
