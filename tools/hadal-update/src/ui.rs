//! Sizes, times and the one progress line that redraws in place

use std::io::{self, IsTerminal, Write};
use std::time::{Duration, Instant};

/// Decimal units, as in "842 KB" for firmware.dfu
pub fn size(bytes: u64) -> String {
    const KB: f64 = 1000.0;
    let b = bytes as f64;
    if bytes < 1000 {
        format!("{bytes} B")
    } else if b < KB * KB {
        format!("{:.0} KB", b / KB)
    } else {
        format!("{:.1} MB", b / (KB * KB))
    }
}

pub fn speed(bytes: u64, elapsed: Duration) -> String {
    let secs = elapsed.as_secs_f64().max(0.001);
    format!("{:.0} KB/s", bytes as f64 / 1000.0 / secs)
}

/// "8.4 s" under a minute, "1:02" from there
pub fn took(d: Duration) -> String {
    if d < Duration::from_secs(60) {
        format!("{:.1} s", d.as_secs_f64())
    } else {
        clock(d)
    }
}

pub fn clock(d: Duration) -> String {
    let s = d.as_secs();
    format!("{}:{:02}", s / 60, s % 60)
}

pub fn bar(fraction: f64, width: usize) -> String {
    let filled = ((fraction.clamp(0.0, 1.0) * width as f64).round() as usize).min(width);
    format!("{}{}", "█".repeat(filled), "░".repeat(width - filled))
}

/// A status line redrawn in place on a terminal; nothing when the output is piped
pub struct Line {
    live: bool,
    shown: usize,
    last: Option<Instant>,
}

impl Line {
    pub fn new() -> Line {
        Line {
            live: io::stdout().is_terminal(),
            shown: 0,
            last: None,
        }
    }

    /// Redraw, at most ~12 times a second unless `now`
    pub fn show(&mut self, text: &str, now: bool) {
        if !self.live {
            return;
        }
        if !now && self.last.is_some_and(|t| t.elapsed() < Duration::from_millis(80)) {
            return;
        }
        let len = text.chars().count();
        let pad = self.shown.saturating_sub(len);
        print!("\r{text}{}", " ".repeat(pad));
        let _ = io::stdout().flush();
        self.shown = len;
        self.last = Some(Instant::now());
    }

    pub fn clear(&mut self) {
        if self.live && self.shown > 0 {
            print!("\r{}\r", " ".repeat(self.shown));
            let _ = io::stdout().flush();
        }
        self.shown = 0;
        self.last = None;
    }
}

pub fn fail(message: &str) {
    let mut lines = message.lines();
    if let Some(first) = lines.next() {
        eprintln!("\n✗ {first}");
    }
    for l in lines {
        eprintln!("  {l}");
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sizes() {
        assert_eq!(size(512), "512 B");
        assert_eq!(size(842_121), "842 KB");
        assert_eq!(size(3_600_000), "3.6 MB");
    }

    #[test]
    fn times() {
        assert_eq!(took(Duration::from_millis(8400)), "8.4 s");
        assert_eq!(took(Duration::from_secs(62)), "1:02");
        assert_eq!(bar(0.5, 4), "██░░");
    }
}
