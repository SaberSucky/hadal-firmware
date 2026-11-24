//! What this tool last saw on a Flipper: each file's size, timestamp and MD5. A file whose size
//! and timestamp are as they were needn't be hashed on the Flipper again, which was most of the
//! time spent comparing (the Flipper hashes about 1 MB/s). Kept per Flipper, by its hardware UID,
//! in %LOCALAPPDATA%\hadal-update\.

use std::collections::BTreeMap;
use std::env;
use std::fs;
use std::path::PathBuf;

#[derive(Clone, Debug, PartialEq)]
pub struct Seen {
    pub size: u32,
    pub timestamp: u32,
    pub md5: String,
}

pub struct Cache {
    file: Option<PathBuf>,
    seen: BTreeMap<String, Seen>,
}

impl Cache {
    /// The cache for the Flipper with this UID; an empty one if there's none yet or no UID
    pub fn load(uid: &str) -> Cache {
        let uid: String = uid.chars().filter(char::is_ascii_alphanumeric).collect();
        let file = (!uid.is_empty()).then(|| {
            env::var_os("LOCALAPPDATA")
                .map(PathBuf::from)
                .unwrap_or_else(env::temp_dir)
                .join("hadal-update")
                .join(format!("{uid}.txt"))
        });
        let text = file
            .as_ref()
            .and_then(|f| fs::read_to_string(f).ok())
            .unwrap_or_default();
        Cache {
            file,
            seen: parse(&text),
        }
    }

    #[cfg(test)]
    pub fn in_memory() -> Cache {
        Cache {
            file: None,
            seen: BTreeMap::new(),
        }
    }

    /// The MD5 recorded for `path`, if its size and timestamp are still these
    pub fn md5(&self, path: &str, size: u32, timestamp: u32) -> Option<&str> {
        match self.seen.get(path) {
            // 0 is what a file without a date gives: it says nothing about changes
            Some(s) if timestamp != 0 && s.size == size && s.timestamp == timestamp => {
                Some(&s.md5)
            }
            _ => None,
        }
    }

    pub fn set(&mut self, path: &str, seen: Seen) {
        self.seen.insert(path.to_string(), seen);
    }

    pub fn forget(&mut self, path: &str) {
        self.seen.remove(path);
    }

    /// Best effort: without it, the next run hashes on the Flipper as before
    pub fn save(&self) {
        let Some(file) = &self.file else { return };
        let mut text = String::new();
        for (path, s) in &self.seen {
            text.push_str(&format!("{}\t{}\t{}\t{path}\n", s.size, s.timestamp, s.md5));
        }
        if let Some(dir) = file.parent() {
            let _ = fs::create_dir_all(dir);
        }
        let _ = fs::write(file, text);
    }
}

fn parse(text: &str) -> BTreeMap<String, Seen> {
    let mut seen = BTreeMap::new();
    for line in text.lines() {
        let mut parts = line.splitn(4, '\t');
        let (Some(size), Some(timestamp), Some(md5), Some(path)) =
            (parts.next(), parts.next(), parts.next(), parts.next())
        else {
            continue;
        };
        let (Ok(size), Ok(timestamp)) = (size.parse(), timestamp.parse()) else {
            continue;
        };
        if md5.len() == 32 {
            seen.insert(
                path.to_string(),
                Seen {
                    size,
                    timestamp,
                    md5: md5.to_string(),
                },
            );
        }
    }
    seen
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip_and_checks() {
        let mut c = Cache::in_memory();
        let md5 = "900150983cd24fb0d6963f7d28e17f72".to_string();
        c.set("/ext/update/x/a.ths", Seen { size: 3, timestamp: 1000, md5: md5.clone() });
        assert_eq!(c.md5("/ext/update/x/a.ths", 3, 1000), Some(md5.as_str()));
        assert_eq!(c.md5("/ext/update/x/a.ths", 3, 1002), None); // rewritten since
        assert_eq!(c.md5("/ext/update/x/a.ths", 4, 1000), None);
        c.set("/ext/update/x/b.ths", Seen { size: 3, timestamp: 0, md5: md5.clone() });
        assert_eq!(c.md5("/ext/update/x/b.ths", 3, 0), None);

        let text = "3\t1000\t900150983cd24fb0d6963f7d28e17f72\t/ext/update/x/a b.ths\nbad line\n";
        let seen = parse(text);
        assert_eq!(seen.len(), 1);
        assert_eq!(seen["/ext/update/x/a b.ths"].timestamp, 1000);
    }
}
