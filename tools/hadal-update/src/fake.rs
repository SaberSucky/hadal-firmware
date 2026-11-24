//! A pretend Flipper for the tests: the command line's echo and prompt, and the RPC storage,
//! device info and update calls this tool makes, over an in-memory SD card

use std::cell::RefCell;
use std::collections::{BTreeMap, BTreeSet, VecDeque};
use std::io::{self, Read, Write};
use std::rc::Rc;

use md5::{Digest, Md5};
use prost::Message;

use crate::link::Port;
use crate::proto::*;

#[derive(Default)]
pub struct Card {
    pub files: BTreeMap<String, Vec<u8>>,
    pub dirs: BTreeSet<String>,
    /// Pieces of file data received, by size
    pub pieces: Vec<usize>,
    pub app_open: bool,
    pub armed: Option<String>,
    pub rebooted: bool,
    /// FAT timestamps: 2 s apart, one tick per file written
    pub stamps: BTreeMap<String, u32>,
    clock: u32,
    pub md5_requests: usize,
    /// The most bytes of requests (writes aside) the tool sent before reading an answer
    pub most_unanswered: usize,
}

impl Card {
    /// A file as if written now
    pub fn put(&mut self, path: &str, data: &[u8]) {
        self.files.insert(path.to_string(), data.to_vec());
        self.touch(path);
    }

    fn touch(&mut self, path: &str) {
        self.clock += 2;
        self.stamps.insert(path.to_string(), 1_700_000_000 + self.clock);
    }
}

pub struct Fake {
    pub card: Rc<RefCell<Card>>,
    rpc: bool,
    input: Vec<u8>,
    output: VecDeque<u8>,
    writing: Option<(u32, String)>,
    unanswered: usize,
}

impl Fake {
    pub fn new(card: Rc<RefCell<Card>>) -> Fake {
        let mut f = Fake {
            card,
            rpc: false,
            input: Vec::new(),
            output: VecDeque::new(),
            writing: None,
            unanswered: 0,
        };
        f.out(b"\r\n_.-------.._  Welcome to Flipper Zero Command Line Interface!\r\n\r\n>: ");
        f
    }

    fn out(&mut self, b: &[u8]) {
        self.output.extend(b);
    }

    fn reply(&mut self, id: u32, status: i32, has_next: bool, content: Option<Content>) {
        let m = Main {
            command_id: id,
            command_status: status,
            has_next,
            content,
        };
        let bytes = m.encode_length_delimited_to_vec();
        self.out(&bytes);
    }

    fn process(&mut self) {
        loop {
            if !self.rpc {
                let Some(end) = self.input.iter().position(|&b| b == b'\r') else {
                    return;
                };
                let line: Vec<u8> = self.input.drain(..=end).collect();
                let cmd = String::from_utf8_lossy(&line[..line.len() - 1]).to_string();
                self.out(cmd.as_bytes());
                self.out(b"\r\n");
                match cmd.as_str() {
                    "start_rpc_session" => self.rpc = true,
                    "loader info" => self.out(b"No application is running\r\n\r\n>: "),
                    _ => self.out(b"\r\n>: "),
                }
            } else {
                let mut slice = &self.input[..];
                let Ok(len) = prost::encoding::decode_varint(&mut slice) else {
                    return;
                };
                let head = self.input.len() - slice.len();
                if slice.len() < len as usize {
                    return;
                }
                let m = Main::decode(&slice[..len as usize]).expect("a message the fake can read");
                self.input.drain(..head + len as usize);
                if !matches!(m.content, Some(Content::WriteRequest(_))) {
                    self.unanswered += head + len as usize;
                    let mut card = self.card.borrow_mut();
                    card.most_unanswered = card.most_unanswered.max(self.unanswered);
                }
                self.handle(m);
            }
        }
    }

    fn handle(&mut self, m: Main) {
        let id = m.command_id;
        let card = self.card.clone();
        let mut card = card.borrow_mut();
        match m.content {
            Some(Content::PingRequest(p)) => {
                self.reply(id, 0, false, Some(Content::PingResponse(p)))
            }
            Some(Content::DeviceInfoRequest(_)) => {
                let kv = [
                    ("hardware_name", "Fakey"),
                    ("firmware_version", "hadal-test"),
                    ("firmware_commit", "0123abcd"),
                    ("firmware_build_date", "01-01-2026"),
                ];
                for (i, (k, v)) in kv.iter().enumerate() {
                    self.reply(
                        id,
                        0,
                        i + 1 < kv.len(),
                        Some(Content::DeviceInfoResponse(DeviceInfoResponse {
                            key: k.to_string(),
                            value: v.to_string(),
                        })),
                    );
                }
            }
            Some(Content::ListRequest(r)) => {
                if !card.dirs.contains(&r.path) {
                    return self.reply(id, STATUS_STORAGE_NOT_EXIST, false, None);
                }
                let prefix = format!("{}/", r.path);
                let entries: Vec<File> = card
                    .files
                    .iter()
                    .filter_map(|(p, d)| {
                        let name = p.strip_prefix(&prefix)?;
                        (!name.contains('/')).then(|| File {
                            name: name.to_string(),
                            size: d.len() as u32,
                            ..Default::default()
                        })
                    })
                    .collect();
                let chunks: Vec<&[File]> = entries.chunks(8).collect();
                if chunks.is_empty() {
                    return self.reply(id, 0, false, Some(Content::ListResponse(ListResponse::default())));
                }
                for (i, c) in chunks.iter().enumerate() {
                    self.reply(
                        id,
                        0,
                        i + 1 < chunks.len(),
                        Some(Content::ListResponse(ListResponse { file: c.to_vec() })),
                    );
                }
            }
            Some(Content::TimestampRequest(r)) => match card.stamps.get(&r.path) {
                Some(&timestamp) => self.reply(
                    id,
                    0,
                    false,
                    Some(Content::TimestampResponse(TimestampResponse { timestamp })),
                ),
                None => self.reply(id, STATUS_STORAGE_NOT_EXIST, false, None),
            },
            Some(Content::Md5sumRequest(r)) => match {
                card.md5_requests += 1;
                card.files.get(&r.path)
            } {
                Some(d) => {
                    let md5sum = format!("{:x}", Md5::digest(d));
                    self.reply(id, 0, false, Some(Content::Md5sumResponse(Md5sumResponse { md5sum })))
                }
                None => self.reply(id, STATUS_STORAGE_NOT_EXIST, false, None),
            },
            Some(Content::StatRequest(r)) => match card.files.get(&r.path) {
                Some(d) => {
                    let file = Some(File {
                        size: d.len() as u32,
                        ..Default::default()
                    });
                    self.reply(id, 0, false, Some(Content::StatResponse(StatResponse { file })))
                }
                None => self.reply(id, STATUS_STORAGE_NOT_EXIST, false, None),
            },
            Some(Content::MkdirRequest(r)) => {
                let status = if card.dirs.insert(r.path) { 0 } else { STATUS_STORAGE_EXIST };
                self.reply(id, status, false, None)
            }
            Some(Content::DeleteRequest(r)) => {
                card.stamps.remove(&r.path);
                let status = if card.files.remove(&r.path).is_some() { 0 } else { STATUS_STORAGE_NOT_EXIST };
                self.reply(id, status, false, None)
            }
            Some(Content::WriteRequest(r)) => {
                if self.writing.as_ref().map(|(i, _)| *i) != Some(id) {
                    card.put(&r.path, &[]);
                    self.writing = Some((id, r.path.clone()));
                }
                let data = r.file.map(|f| f.data).unwrap_or_default();
                card.pieces.push(data.len());
                card.files.get_mut(&r.path).unwrap().extend_from_slice(&data);
                if !m.has_next {
                    self.writing = None;
                    self.reply(id, 0, false, None);
                }
            }
            Some(Content::ReadRequest(r)) => match card.files.get(&r.path).cloned() {
                Some(d) => {
                    let d = d[(r.offset as usize).min(d.len())..].to_vec();
                    if d.is_empty() {
                        let file = Some(File::default());
                        let content = Content::ReadResponse(ReadResponse { file, ranged: true });
                        return self.reply(id, 0, false, Some(content));
                    }
                    let chunks: Vec<&[u8]> = d.chunks(512).collect();
                    for (i, c) in chunks.iter().enumerate() {
                        let file = Some(File {
                            data: c.to_vec(),
                            ..Default::default()
                        });
                        self.reply(
                            id,
                            0,
                            i + 1 < chunks.len(),
                            Some(Content::ReadResponse(ReadResponse { file, ranged: false })),
                        );
                    }
                }
                None => self.reply(id, STATUS_STORAGE_NOT_EXIST, false, None),
            },
            Some(Content::LockStatusRequest(_)) => {
                let locked = card.app_open;
                self.reply(id, 0, false, Some(Content::LockStatusResponse(LockStatusResponse { locked })))
            }
            Some(Content::UpdateRequest(r)) => {
                let code = if card.files.contains_key(&r.update_manifest) { 0 } else { 2 };
                if code == 0 {
                    card.armed = Some(r.update_manifest);
                }
                let status = if code == 0 { 0 } else { 15 };
                self.reply(id, status, false, Some(Content::UpdateResponse(UpdateResponse { code })))
            }
            Some(Content::RebootRequest(_)) => card.rebooted = true,
            Some(Content::StopSession(_)) => {
                self.reply(id, 0, false, None);
                self.rpc = false;
                self.out(b"\r\n>: ");
            }
            _ => self.reply(id, 3, false, None),
        }
    }
}

impl Read for Fake {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        if self.output.is_empty() {
            return Err(io::ErrorKind::TimedOut.into());
        }
        self.unanswered = 0;
        let n = buf.len().min(self.output.len());
        for (i, b) in self.output.drain(..n).enumerate() {
            buf[i] = b;
        }
        Ok(n)
    }
}

impl Write for Fake {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.input.extend_from_slice(buf);
        self.process();
        Ok(buf.len())
    }

    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

impl Port for Fake {
    fn bytes_to_read(&self) -> io::Result<u32> {
        Ok(self.output.len() as u32)
    }

    fn set_timeout(&mut self, _timeout: std::time::Duration) -> io::Result<()> {
        Ok(())
    }
}
