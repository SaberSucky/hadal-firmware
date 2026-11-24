//! The USB serial port and the Flipper's command line on it

use std::fmt;
use std::io::{self, Read, Write};
use std::time::{Duration, Instant};

use anyhow::{anyhow, bail, Context, Result};
use serialport::{SerialPort, SerialPortType};

pub const FLIPPER_VID: u16 = 0x0483;
pub const FLIPPER_PID: u16 = 0x5740;

const PROMPT: &[u8] = b">: ";
/// How long one read waits when nothing arrives; it returns as soon as anything does
const POLL: Duration = Duration::from_millis(100);

pub struct FlipperPort {
    pub name: String,
    pub label: Option<String>,
}

/// The Flipper's serial ports, by their USB id
pub fn flipper_ports() -> Vec<FlipperPort> {
    let Ok(ports) = serialport::available_ports() else {
        return Vec::new();
    };
    let mut found: Vec<FlipperPort> = Vec::new();
    for p in ports {
        if let SerialPortType::UsbPort(usb) = p.port_type {
            if usb.vid == FLIPPER_VID
                && usb.pid == FLIPPER_PID
                && !found.iter().any(|f| f.name.eq_ignore_ascii_case(&p.port_name))
            {
                found.push(FlipperPort {
                    name: p.port_name,
                    label: usb.serial_number.or(usb.product),
                });
            }
        }
    }
    found
}

pub fn is_present(name: &str) -> bool {
    flipper_ports()
        .iter()
        .any(|p| p.name.eq_ignore_ascii_case(name))
}

/// The port to use: the one asked for, or the only Flipper on USB
pub fn pick_port(asked: Option<&str>) -> Result<FlipperPort> {
    let ports = flipper_ports();
    if let Some(name) = asked {
        let label = ports
            .into_iter()
            .find(|p| p.name.eq_ignore_ascii_case(name))
            .and_then(|p| p.label);
        return Ok(FlipperPort {
            name: name.to_string(),
            label,
        });
    }
    match ports.len() {
        0 => bail!(
            "No Flipper found on USB (looked for USB id {FLIPPER_VID:04x}:{FLIPPER_PID:04x}).\n\
             Plug it in and make sure it's on and unlocked."
        ),
        1 => Ok(ports.into_iter().next().unwrap()),
        _ => {
            let list: Vec<String> = ports
                .iter()
                .map(|p| match &p.label {
                    Some(l) => format!("{} ({l})", p.name),
                    None => p.name.clone(),
                })
                .collect();
            bail!(
                "Several Flippers are plugged in: {}. Choose one with --port.",
                list.join(", ")
            )
        }
    }
}

/// Waiting ran out: the caller may try again
#[derive(Debug)]
pub struct Timeout(pub String);

impl fmt::Display for Timeout {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}

impl std::error::Error for Timeout {}

/// The port went away under us (unplugged, or the Flipper restarted)
#[derive(Debug)]
pub struct Lost(pub String);

impl fmt::Display for Lost {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}

impl std::error::Error for Lost {}

/// What a Link talks through: the serial port, or a fake Flipper in the tests
pub trait Port: Read + Write {
    fn bytes_to_read(&self) -> io::Result<u32>;
    /// How long a read or a write may wait. Windows has one setting for both (COMMTIMEOUTS)
    fn set_timeout(&mut self, timeout: Duration) -> io::Result<()>;
}

impl Port for Box<dyn SerialPort> {
    fn bytes_to_read(&self) -> io::Result<u32> {
        SerialPort::bytes_to_read(&**self).map_err(io::Error::from)
    }

    fn set_timeout(&mut self, timeout: Duration) -> io::Result<()> {
        SerialPort::set_timeout(&mut **self, timeout).map_err(io::Error::from)
    }
}

/// What the port's timeout is set for
#[derive(Clone, Copy, PartialEq)]
enum Waiting {
    /// Reads give up after POLL, so the callers can look at the time
    Reads,
    /// Writes wait as long as the Flipper takes. A write cut short by its timeout is cancelled in
    /// the driver part way through, and what it reports sent then can be wrong: a 32 KB write
    /// under a 100 ms timeout left the Flipper's RPC stream garbled and stuck
    Writes(Duration),
}

pub struct Link {
    port: Box<dyn Port>,
    pub name: String,
    /// Received, not yet used
    pub buf: Vec<u8>,
    waiting: Waiting,
}

impl Link {
    pub fn open(name: &str) -> Result<Link> {
        let mut port = serialport::new(name, 230_400)
            .timeout(POLL)
            .open()
            .map_err(|e| {
                if e.to_string().to_lowercase().contains("denied") {
                    anyhow!(
                        "Couldn't open {name}: {e}\n\
                         Something else has it open (qFlipper, a serial terminal, another updater): close that first."
                    )
                } else {
                    anyhow!("Couldn't open {name}: {e}\nIs the Flipper plugged in, on that port?")
                }
            })?;
        // The Flipper starts its command line when DTR goes up
        port.write_data_terminal_ready(true)
            .with_context(|| format!("Couldn't raise DTR on {name}"))?;
        Ok(Link::over(Box::new(port), name))
    }

    pub fn over(port: Box<dyn Port>, name: &str) -> Link {
        Link {
            port,
            name: name.to_string(),
            buf: Vec::new(),
            waiting: Waiting::Reads,
        }
    }

    fn wait_for(&mut self, waiting: Waiting) -> Result<()> {
        if self.waiting != waiting {
            let timeout = match waiting {
                Waiting::Reads => POLL,
                Waiting::Writes(t) => t,
            };
            self.port
                .set_timeout(timeout)
                .map_err(|e| self.lost(e))?;
            self.waiting = waiting;
        }
        Ok(())
    }

    /// Open the port and wait for the command line's prompt
    pub fn connect(name: &str, wait: Duration) -> Result<Link> {
        let mut link = Link::open(name)?;
        link.sync_cli(wait)?;
        Ok(link)
    }

    fn lost(&self, e: impl fmt::Display) -> anyhow::Error {
        Lost(format!("Lost the connection to the Flipper on {}: {e}", self.name)).into()
    }

    /// Wait up to POLL for data; how many bytes came
    pub fn read_some(&mut self) -> Result<usize> {
        self.wait_for(Waiting::Reads)?;
        let mut tmp = [0u8; 16 * 1024];
        match self.port.read(&mut tmp) {
            Ok(n) => {
                self.buf.extend_from_slice(&tmp[..n]);
                Ok(n)
            }
            Err(e) if matches!(e.kind(), io::ErrorKind::TimedOut | io::ErrorKind::Interrupted) => {
                Ok(0)
            }
            Err(e) => Err(self.lost(e)),
        }
    }

    /// Take what has already arrived, without waiting
    pub fn read_ready(&mut self) -> Result<usize> {
        match self.port.bytes_to_read() {
            Ok(0) => Ok(0),
            Ok(_) => self.read_some(),
            Err(e) => Err(self.lost(e)),
        }
    }

    /// Write everything; fails if the Flipper takes nothing for `stall`
    pub fn write_all(&mut self, mut data: &[u8], stall: Duration) -> Result<()> {
        self.wait_for(Waiting::Writes(stall))?;
        let mut progress = Instant::now();
        while !data.is_empty() {
            match self.port.write(data) {
                Ok(n) if n > 0 => {
                    data = &data[n..];
                    progress = Instant::now();
                }
                Ok(_) => {}
                Err(e)
                    if matches!(e.kind(), io::ErrorKind::TimedOut | io::ErrorKind::Interrupted) => {}
                Err(e) => return Err(self.lost(e)),
            }
            if !data.is_empty() && progress.elapsed() > stall {
                bail!(
                    "The Flipper stopped taking data on {} (nothing went through for {} s)",
                    self.name,
                    stall.as_secs()
                );
            }
        }
        Ok(())
    }

    /// Read until `pat`; returns what came before it and drops `pat` from the buffer
    pub fn read_until(&mut self, pat: &[u8], timeout: Duration) -> Result<Vec<u8>> {
        let deadline = Instant::now() + timeout;
        let mut from = 0;
        loop {
            if let Some(i) = find(&self.buf[from..], pat) {
                let end = from + i;
                let out = self.buf[..end].to_vec();
                self.buf.drain(..end + pat.len());
                return Ok(out);
            }
            from = self.buf.len().saturating_sub(pat.len());
            if Instant::now() > deadline {
                return Err(Timeout(format!(
                    "No answer from the Flipper on {} within {} s",
                    self.name,
                    timeout.as_secs_f32()
                ))
                .into());
            }
            self.read_some()?;
        }
    }

    /// Read until nothing arrives for `quiet` (at most `max`)
    pub fn drain_quiet(&mut self, quiet: Duration, max: Duration) -> Result<()> {
        let start = Instant::now();
        let mut last = Instant::now();
        while start.elapsed() < max {
            if self.read_some()? > 0 {
                last = Instant::now();
            } else if last.elapsed() >= quiet {
                break;
            }
        }
        Ok(())
    }

    pub fn sync_cli(&mut self, wait: Duration) -> Result<()> {
        // The command line prints its banner and a prompt once DTR is up; Enter gets a new prompt
        let start = Instant::now();
        let mut nudged = Instant::now();
        while find(&self.buf, PROMPT).is_none() {
            if start.elapsed() > wait {
                return Err(Timeout(format!(
                    "{} is open, but the Flipper's command line isn't answering",
                    self.name
                ))
                .into());
            }
            if nudged.elapsed() > Duration::from_millis(700) {
                self.write_all(b"\r", Duration::from_secs(2))?;
                nudged = Instant::now();
            }
            self.read_some()?;
        }
        self.drain_quiet(Duration::from_millis(50), Duration::from_secs(2))?;
        self.buf.clear();
        Ok(())
    }

    /// Run a command; its output, without the echo and the next prompt
    pub fn cli(&mut self, cmd: &str, timeout: Duration) -> Result<String> {
        self.write_all(format!("{cmd}\r").as_bytes(), Duration::from_secs(5))?;
        self.read_until(format!("{cmd}\r\n").as_bytes(), Duration::from_secs(3))?;
        let out = self.read_until(PROMPT, timeout)?;
        Ok(clean_text(&out))
    }

    /// Switch the port from the command line to RPC (protobuf). Rpc::start's ping tells
    /// whether the session started
    pub fn start_rpc(&mut self) -> Result<()> {
        self.write_all(b"start_rpc_session\r", Duration::from_secs(5))?;
        self.read_until(b"start_rpc_session\r\n", Duration::from_secs(3))?;
        Ok(())
    }

    /// Keep the port open, reading nothing in particular, until the Flipper goes off USB (it
    /// restarts) or `max` passes; whether it went
    pub fn hold_until_gone(&mut self, max: Duration) -> Result<bool> {
        let start = Instant::now();
        let mut looked = Instant::now();
        while start.elapsed() < max {
            match self.read_some() {
                Ok(_) => self.buf.clear(),
                Err(e) if e.is::<Lost>() => return Ok(true),
                Err(e) => return Err(e),
            }
            // a reset doesn't always fail the read at once: look at USB too
            if looked.elapsed() > Duration::from_millis(250) {
                if !is_present(&self.name) {
                    return Ok(true);
                }
                looked = Instant::now();
            }
        }
        Ok(false)
    }
}

pub fn find(hay: &[u8], needle: &[u8]) -> Option<usize> {
    if needle.is_empty() || hay.len() < needle.len() {
        return None;
    }
    hay.windows(needle.len()).position(|w| w == needle)
}

/// Text from the command line, without colour codes
pub fn clean_text(raw: &[u8]) -> String {
    let text = String::from_utf8_lossy(raw);
    let mut out = String::with_capacity(text.len());
    let mut chars = text.chars().peekable();
    while let Some(c) = chars.next() {
        if c == '\x1b' {
            // CSI: ESC [ ... final byte in @..~
            if chars.peek() == Some(&'[') {
                chars.next();
                for c in chars.by_ref() {
                    if ('@'..='~').contains(&c) {
                        break;
                    }
                }
            }
            continue;
        }
        if c != '\r' {
            out.push(c);
        }
    }
    out.trim().to_string()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn strips_colour_codes() {
        assert_eq!(
            clean_text(b"\x1b[31mthis command cannot be run\x1b[0m\r\n"),
            "this command cannot be run"
        );
        assert_eq!(
            clean_text(b"Application \"UpdaterApp\" is running\r\n"),
            "Application \"UpdaterApp\" is running"
        );
    }

    #[test]
    fn finds() {
        assert_eq!(find(b"abc>: ", PROMPT), Some(3));
        assert_eq!(find(b">", PROMPT), None);
    }
}
