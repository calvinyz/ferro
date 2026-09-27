//! Per-tick simulator state written to CSV, for replay and rendering.

use crate::record::TickRecord;
use std::fs::File;
use std::io::{self, BufWriter, Write};

pub struct Trajectory {
    out: BufWriter<File>,
    wrote_header: bool,
}

impl Trajectory {
    pub fn create(path: &str) -> io::Result<Self> {
        Ok(Self {
            out: BufWriter::new(File::create(path)?),
            wrote_header: false,
        })
    }

    pub fn write(&mut self, r: &TickRecord) -> io::Result<()> {
        let nq = usize::from(r.qpos_dim).min(r.qpos.len());
        let nu = usize::from(r.action_dim).min(r.action.len());

        if !self.wrote_header {
            write!(self.out, "tick,timestamp_ns")?;
            for i in 0..nq {
                write!(self.out, ",qpos{i}")?;
            }
            for i in 0..nu {
                write!(self.out, ",action{i}")?;
            }
            writeln!(self.out)?;
            self.wrote_header = true;
        }

        write!(self.out, "{},{}", r.tick, r.timestamp_ns)?;
        for v in &r.qpos[..nq] {
            write!(self.out, ",{v}")?;
        }
        for v in &r.action[..nu] {
            write!(self.out, ",{v}")?;
        }
        writeln!(self.out)
    }

    pub fn flush(&mut self) -> io::Result<()> {
        self.out.flush()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn writes_header_once_and_only_valid_dims() {
        let path = std::env::temp_dir().join("ferro_trajectory_test.csv");
        let path = path.to_str().unwrap();

        let mut t = Trajectory::create(path).unwrap();
        for tick in 0..2 {
            let mut r = TickRecord { tick, qpos_dim: 2, action_dim: 1, ..Default::default() };
            r.qpos[0] = 1.5;
            r.qpos[1] = -2.0;
            r.qpos[2] = 99.0; // past qpos_dim, must not appear
            r.action[0] = 0.25;
            t.write(&r).unwrap();
        }
        t.flush().unwrap();

        let text = std::fs::read_to_string(path).unwrap();
        let lines: Vec<&str> = text.lines().collect();
        assert_eq!(lines[0], "tick,timestamp_ns,qpos0,qpos1,action0");
        assert_eq!(lines[1], "0,0,1.5,-2,0.25");
        assert_eq!(lines.len(), 3);
    }
}
