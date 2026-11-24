//! An RPC session over the Flipper's serial port: length-prefixed protobuf PB.Main messages

use std::collections::BTreeMap;
use std::fmt;
use std::time::{Duration, Instant};

use anyhow::{anyhow, bail, Result};
use prost::Message;

use crate::link::{Link, Timeout};
use crate::proto::*;

/// A write that makes no progress for this long has stalled
const STALL: Duration = Duration::from_secs(30);

/// The Flipper answered a request with an error status
#[derive(Debug)]
pub struct Refused {
    pub what: String,
    pub status: i32,
}

impl fmt::Display for Refused {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "The Flipper refused to {}: {} (status {})",
            self.what,
            status_text(self.status),
            self.status
        )
    }
}

impl std::error::Error for Refused {}

fn refused_with(e: &anyhow::Error, status: i32) -> bool {
    e.downcast_ref::<Refused>().is_some_and(|r| r.status == status)
}

pub struct RemoteFile {
    pub name: String,
    pub size: u32,
    pub is_dir: bool,
}

pub struct Rpc {
    pub link: Link,
    next_id: u32,
    verbose: bool,
}

impl Rpc {
    /// Start a session on a port whose command line is at its prompt
    pub fn start(mut link: Link, verbose: bool) -> Result<Rpc> {
        link.start_rpc()?;
        let mut rpc = Rpc {
            link,
            next_id: 1,
            verbose,
        };
        rpc.ping()?;
        Ok(rpc)
    }

    fn new_id(&mut self) -> u32 {
        let id = self.next_id;
        self.next_id += 1;
        id
    }

    fn send(&mut self, id: u32, has_next: bool, content: Content) -> Result<()> {
        let m = Main {
            command_id: id,
            command_status: 0,
            has_next,
            content: Some(content),
        };
        if self.verbose {
            eprintln!("    rpc > #{id}{} {}", if has_next { "+" } else { "" }, describe(&m));
        }
        self.link.write_all(&m.encode_length_delimited_to_vec(), STALL)
    }

    /// A whole message out of what has arrived, if there is one
    fn take_message(&mut self) -> Result<Option<Main>> {
        let buf = &self.link.buf;
        let mut len: u64 = 0;
        let mut head = 0;
        loop {
            if head >= buf.len() {
                return Ok(None);
            }
            if head >= 10 {
                bail!("Garbled data from the Flipper (a message length that never ends)");
            }
            let b = buf[head];
            len |= u64::from(b & 0x7f) << (7 * head);
            head += 1;
            if b & 0x80 == 0 {
                break;
            }
        }
        let len = usize::try_from(len).unwrap_or(usize::MAX);
        if len > 1 << 20 {
            bail!("Garbled data from the Flipper (a message of {len} bytes)");
        }
        if buf.len() < head + len {
            return Ok(None);
        }
        let m = Main::decode(&buf[head..head + len])
            .map_err(|e| anyhow!("Couldn't decode a message from the Flipper: {e}"))?;
        self.link.buf.drain(..head + len);
        if self.verbose {
            eprintln!(
                "    rpc < #{}{} {} {}",
                m.command_id,
                if m.has_next { "+" } else { "" },
                status_text(m.command_status),
                describe(&m)
            );
        }
        Ok(Some(m))
    }

    fn receive(&mut self, what: &str, timeout: Duration) -> Result<Main> {
        let deadline = Instant::now() + timeout;
        loop {
            if let Some(m) = self.take_message()? {
                return Ok(m);
            }
            if Instant::now() > deadline {
                return Err(Timeout(format!(
                    "The Flipper didn't answer ({what}) within {} s",
                    timeout.as_secs()
                ))
                .into());
            }
            self.link.read_some()?;
        }
    }

    /// The answers to command `id`, up to the one without has_next, whatever their status
    fn collect_any(&mut self, id: u32, what: &str, timeout: Duration) -> Result<Vec<Main>> {
        let mut out = Vec::new();
        loop {
            let m = self.receive(what, timeout)?;
            if m.command_id != id {
                continue; // a late answer to something earlier
            }
            let more = m.has_next && m.command_status == STATUS_OK;
            out.push(m);
            if !more {
                return Ok(out);
            }
        }
    }

    fn collect(&mut self, id: u32, what: &str, timeout: Duration) -> Result<Vec<Main>> {
        let out = self.collect_any(id, what, timeout)?;
        if let Some(bad) = out.iter().find(|m| m.command_status != STATUS_OK) {
            return Err(Refused {
                what: what.to_string(),
                status: bad.command_status,
            }
            .into());
        }
        Ok(out)
    }

    fn call(&mut self, what: &str, content: Content, timeout: Duration) -> Result<Vec<Main>> {
        let id = self.new_id();
        self.send(id, false, content)?;
        self.collect(id, what, timeout)
    }

    pub fn ping(&mut self) -> Result<()> {
        let data = b"hadal-update".to_vec();
        let answers = self.call(
            "answer a ping",
            Content::PingRequest(Ping { data: data.clone() }),
            Duration::from_secs(3),
        )?;
        match answers.last().and_then(|m| m.content.as_ref()) {
            Some(Content::PingResponse(p)) if p.data == data => Ok(()),
            _ => bail!("The Flipper's RPC answered a ping with something else"),
        }
    }

    /// device_info's keys and values (firmware_version, firmware_commit, hardware_name, ...)
    pub fn device_info(&mut self) -> Result<BTreeMap<String, String>> {
        let answers = self.call(
            "give its device info",
            Content::DeviceInfoRequest(Empty {}),
            Duration::from_secs(5),
        )?;
        let mut info = BTreeMap::new();
        for m in answers {
            if let Some(Content::DeviceInfoResponse(kv)) = m.content {
                info.insert(kv.key, kv.value);
            }
        }
        Ok(info)
    }

    /// A folder's entries; None if it doesn't exist
    pub fn list(&mut self, path: &str) -> Result<Option<Vec<RemoteFile>>> {
        let answers = match self.call(
            &format!("list {path}"),
            Content::ListRequest(ListRequest {
                path: path.to_string(),
                ..Default::default()
            }),
            Duration::from_secs(15),
        ) {
            Ok(a) => a,
            Err(e) if refused_with(&e, STATUS_STORAGE_NOT_EXIST) => return Ok(None),
            Err(e) => return Err(e),
        };
        let mut files = Vec::new();
        for m in answers {
            if let Some(Content::ListResponse(list)) = m.content {
                files.extend(list.file.into_iter().map(|f| RemoteFile {
                    is_dir: f.file_type == FILE_TYPE_DIR,
                    name: f.name,
                    size: f.size,
                }));
            }
        }
        Ok(Some(files))
    }

    /// A file's size; None if it doesn't exist
    pub fn stat_size(&mut self, path: &str) -> Result<Option<u32>> {
        match self.call(
            &format!("stat {path}"),
            Content::StatRequest(PathRequest {
                path: path.to_string(),
            }),
            Duration::from_secs(10),
        ) {
            Ok(answers) => Ok(answers.into_iter().find_map(|m| match m.content {
                Some(Content::StatResponse(s)) => s.file.map(|f| f.size),
                _ => None,
            })),
            Err(e) if refused_with(&e, STATUS_STORAGE_NOT_EXIST) => Ok(None),
            Err(e) => Err(e),
        }
    }

    /// The MD5 of a file on the Flipper, as lowercase hex
    pub fn md5(&mut self, path: &str) -> Result<String> {
        let answers = self.call(
            &format!("hash {path}"),
            Content::Md5sumRequest(PathRequest {
                path: path.to_string(),
            }),
            Duration::from_secs(60),
        )?;
        answers
            .into_iter()
            .find_map(|m| match m.content {
                Some(Content::Md5sumResponse(r)) => Some(r.md5sum.to_ascii_lowercase()),
                _ => None,
            })
            .ok_or_else(|| anyhow!("The Flipper sent no MD5 for {path}"))
    }

    /// Make a folder; one that's already there is fine
    pub fn mkdir(&mut self, path: &str) -> Result<()> {
        match self.call(
            &format!("make the folder {path}"),
            Content::MkdirRequest(PathRequest {
                path: path.to_string(),
            }),
            Duration::from_secs(10),
        ) {
            Ok(_) => Ok(()),
            Err(e) if refused_with(&e, STATUS_STORAGE_EXIST) => Ok(()),
            Err(e) => Err(e),
        }
    }

    pub fn delete(&mut self, path: &str) -> Result<()> {
        self.call(
            &format!("delete {path}"),
            Content::DeleteRequest(DeleteRequest {
                path: path.to_string(),
                recursive: false,
            }),
            Duration::from_secs(15),
        )
        .map(|_| ())
    }

    pub fn read_file(&mut self, path: &str) -> Result<Vec<u8>> {
        let answers = self.call(
            &format!("read {path}"),
            Content::ReadRequest(ReadRequest {
                path: path.to_string(),
                ..Default::default()
            }),
            Duration::from_secs(15),
        )?;
        let mut data = Vec::new();
        for m in answers {
            if let Some(Content::ReadResponse(ReadResponse {
                file: Some(f), ..
            })) = m.content
            {
                data.extend_from_slice(&f.data);
            }
        }
        Ok(data)
    }

    /// Write a file in pieces of `chunk` bytes; `progress` gets the bytes sent so far
    pub fn write_file(
        &mut self,
        path: &str,
        data: &[u8],
        chunk: usize,
        mut progress: impl FnMut(usize),
    ) -> Result<()> {
        let id = self.new_id();
        let what = format!("write {path}");
        let pieces: Vec<&[u8]> = if data.is_empty() {
            vec![data]
        } else {
            data.chunks(chunk).collect()
        };
        let last = pieces.len() - 1;
        let mut sent = 0;
        for (i, piece) in pieces.into_iter().enumerate() {
            let has_next = i < last;
            self.send(
                id,
                has_next,
                Content::WriteRequest(WriteRequest {
                    path: path.to_string(),
                    file: Some(File {
                        data: piece.to_vec(),
                        ..Default::default()
                    }),
                }),
            )?;
            sent += piece.len();
            progress(sent);
            if has_next {
                // The Flipper answers once, after the last piece, unless a write fails: then
                // it answers at once and drops the rest
                self.link.read_ready()?;
                while let Some(m) = self.take_message()? {
                    if m.command_id == id {
                        let status = if m.command_status == STATUS_OK {
                            1
                        } else {
                            m.command_status
                        };
                        return Err(Refused { what, status }.into());
                    }
                }
            }
        }
        self.collect(id, &what, Duration::from_secs(60)).map(|_| ())
    }

    /// Whether an app is open (the loader is locked)
    pub fn app_open(&mut self) -> Result<bool> {
        let answers = self.call(
            "say whether an app is open",
            Content::LockStatusRequest(Empty {}),
            Duration::from_secs(5),
        )?;
        Ok(answers.into_iter().any(|m| {
            matches!(
                m.content,
                Some(Content::LockStatusResponse(LockStatusResponse { locked: true }))
            )
        }))
    }

    /// Check the package and arm the update; the firmware's UpdatePrepareResult (0 is OK)
    pub fn prepare_update(&mut self, manifest: &str) -> Result<i32> {
        let id = self.new_id();
        self.send(
            id,
            false,
            Content::UpdateRequest(UpdateRequest {
                update_manifest: manifest.to_string(),
            }),
        )?;
        let answers = self.collect_any(id, "check the package", Duration::from_secs(60))?;
        let last = answers.last().expect("collect_any returns at least one");
        match &last.content {
            Some(Content::UpdateResponse(r)) => Ok(r.code),
            _ if last.command_status != STATUS_OK => Err(Refused {
                what: "check the package".into(),
                status: last.command_status,
            }
            .into()),
            _ => Ok(0),
        }
    }

    /// Restart into the updater; the Flipper doesn't answer, it restarts
    pub fn reboot_to_update(&mut self) -> Result<()> {
        let id = self.new_id();
        self.send(
            id,
            false,
            Content::RebootRequest(RebootRequest {
                mode: REBOOT_MODE_UPDATE,
            }),
        )
    }

    /// End the session; the port goes back to the command line, then closes
    pub fn stop(mut self) {
        let id = self.new_id();
        if self.send(id, false, Content::StopSession(Empty {})).is_ok() {
            let _ = self.collect_any(id, "stop the session", Duration::from_secs(1));
        }
    }
}

fn describe(m: &Main) -> String {
    match &m.content {
        None => "(nothing this tool reads)".into(),
        Some(c) => match c {
            Content::Empty(_) => "empty".into(),
            Content::StopSession(_) => "stop session".into(),
            Content::PingRequest(_) => "ping".into(),
            Content::PingResponse(_) => "pong".into(),
            Content::RebootRequest(r) => format!("reboot mode {}", r.mode),
            Content::DeviceInfoRequest(_) => "device info?".into(),
            Content::DeviceInfoResponse(kv) => format!("{} = {}", kv.key, kv.value),
            Content::UpdateRequest(r) => format!("update {}", r.update_manifest),
            Content::UpdateResponse(r) => format!("update result {}", r.code),
            Content::LockStatusRequest(_) => "app open?".into(),
            Content::LockStatusResponse(r) => format!("app open: {}", r.locked),
            Content::StatRequest(r) => format!("stat {}", r.path),
            Content::StatResponse(r) => format!("stat: {:?}", r.file.as_ref().map(|f| f.size)),
            Content::ListRequest(r) => format!("list {}", r.path),
            Content::ListResponse(r) => format!("{} entries", r.file.len()),
            Content::ReadRequest(r) => format!("read {}", r.path),
            Content::ReadResponse(r) => {
                format!("{} bytes", r.file.as_ref().map_or(0, |f| f.data.len()))
            }
            Content::WriteRequest(r) => format!(
                "write {} ({} bytes)",
                r.path,
                r.file.as_ref().map_or(0, |f| f.data.len())
            ),
            Content::DeleteRequest(r) => format!("delete {}", r.path),
            Content::MkdirRequest(r) => format!("mkdir {}", r.path),
            Content::Md5sumRequest(r) => format!("md5 {}", r.path),
            Content::Md5sumResponse(r) => format!("md5 {}", r.md5sum),
        },
    }
}
