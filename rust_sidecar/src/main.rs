fn main() {
    println!(
        "ferro_sidecar: ring is {} bytes, {} slots of {} bytes",
        ferro_sidecar::ferro_sidecar_ring_size(),
        ferro_sidecar::record::RING_CAPACITY,
        size_of::<ferro_sidecar::record::TickRecord>()
    );
}
