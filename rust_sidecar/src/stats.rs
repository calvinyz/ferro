//! Consumer-side aggregation over popped tick records.

use crate::record::TickRecord;

#[derive(Debug, Default, Clone, Copy)]
pub struct SidecarStats {
    pub popped: u64,
    pub dropped: u64,
    pub first_tick: u64,
    pub last_tick: u64,
    pub inference_ns_max: u32,
    pub tick_work_ns_max: u32,
    inference_ns_sum: u64,
    tick_work_ns_sum: u64,
    seen_any: bool,
}

impl SidecarStats {
    pub fn new() -> Self {
        Self::default()
    }

    /// Producer drops are inferred from gaps in the tick sequence, so the ring
    /// needs no shared drop counter and the layout contract stays unchanged.
    pub fn record(&mut self, r: &TickRecord) {
        if self.seen_any {
            self.dropped += r.tick.saturating_sub(self.last_tick).saturating_sub(1);
        } else {
            self.first_tick = r.tick;
            self.seen_any = true;
        }

        self.last_tick = r.tick;
        self.popped += 1;
        self.inference_ns_sum += u64::from(r.inference_ns);
        self.tick_work_ns_sum += u64::from(r.tick_work_ns);
        self.inference_ns_max = self.inference_ns_max.max(r.inference_ns);
        self.tick_work_ns_max = self.tick_work_ns_max.max(r.tick_work_ns);
    }

    pub fn mean_inference_ns(&self) -> f64 {
        self.mean(self.inference_ns_sum)
    }

    pub fn mean_tick_work_ns(&self) -> f64 {
        self.mean(self.tick_work_ns_sum)
    }

    fn mean(&self, sum: u64) -> f64 {
        if self.popped == 0 {
            0.0
        } else {
            sum as f64 / self.popped as f64
        }
    }
}

impl std::fmt::Display for SidecarStats {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "sidecar: popped={} dropped={} ticks={}..{} inference mean={:.1}us max={:.1}us \
             tick_work mean={:.1}us max={:.1}us",
            self.popped,
            self.dropped,
            self.first_tick,
            self.last_tick,
            self.mean_inference_ns() / 1000.0,
            f64::from(self.inference_ns_max) / 1000.0,
            self.mean_tick_work_ns() / 1000.0,
            f64::from(self.tick_work_ns_max) / 1000.0,
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rec(tick: u64, inference_ns: u32, tick_work_ns: u32) -> TickRecord {
        TickRecord {
            tick,
            timestamp_ns: 0,
            obs: [0.0; crate::record::OBS_DIM],
            action: 0.0,
            inference_ns,
            tick_work_ns,
            _pad: [0; 5],
        }
    }

    #[test]
    fn empty_stats_do_not_divide_by_zero() {
        let s = SidecarStats::new();
        assert_eq!(s.popped, 0);
        assert_eq!(s.mean_inference_ns(), 0.0);
    }

    #[test]
    fn accumulates_means_and_maxima() {
        let mut s = SidecarStats::new();
        s.record(&rec(0, 100, 200));
        s.record(&rec(1, 300, 400));

        assert_eq!(s.popped, 2);
        assert_eq!(s.dropped, 0);
        assert_eq!(s.mean_inference_ns(), 200.0);
        assert_eq!(s.inference_ns_max, 300);
        assert_eq!(s.tick_work_ns_max, 400);
    }

    #[test]
    fn infers_drops_from_tick_gaps() {
        let mut s = SidecarStats::new();
        s.record(&rec(0, 0, 0));
        s.record(&rec(5, 0, 0));

        assert_eq!(s.popped, 2);
        assert_eq!(s.dropped, 4);
        assert_eq!(s.first_tick, 0);
        assert_eq!(s.last_tick, 5);
    }

    #[test]
    fn first_record_is_never_counted_as_a_drop() {
        let mut s = SidecarStats::new();
        s.record(&rec(9000, 0, 0));

        assert_eq!(s.dropped, 0);
        assert_eq!(s.first_tick, 9000);
    }
}
