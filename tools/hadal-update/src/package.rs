//! A built update package: its folder (dist/f7-C/f7-update-local), its update.fuf, or its .tgz

use std::env;
use std::fs;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::time::SystemTime;

use anyhow::{anyhow, bail, Context, Result};
use md5::{Digest, Md5};

pub const MANIFEST: &str = "update.fuf";

pub struct PkgFile {
    pub name: String,
    pub data: Vec<u8>,
    pub md5: String,
}

impl PkgFile {
    fn new(name: String, data: Vec<u8>) -> PkgFile {
        let md5 = format!("{:x}", Md5::digest(&data));
        PkgFile { name, data, md5 }
    }

    pub fn len(&self) -> u64 {
        self.data.len() as u64
    }
}

/// What fbt says it built (flipper-z-f7-full-*.json beside the package)
pub struct Build {
    pub version: String,
    pub commit: String,
    pub date: String,
}

pub struct Package {
    /// The package folder's name, which is also its folder under /ext/update/
    pub name: String,
    /// Its files, by name, with update.fuf last: it goes up once everything it names is there
    pub files: Vec<PkgFile>,
    pub build: Option<Build>,
    /// Files in the folder that aren't sent (subfolders)
    pub skipped: Vec<String>,
}

impl Package {
    pub fn total(&self) -> u64 {
        self.files.iter().map(PkgFile::len).sum()
    }
}

pub fn load(path: &Path) -> Result<Package> {
    let meta = fs::metadata(path).with_context(|| format!("Can't read {}", path.display()))?;
    let full = fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf());
    let (name, files, skipped, beside) = if meta.is_dir() {
        if !full.join(MANIFEST).is_file() {
            bail!(
                "{} has no {MANIFEST}, so it isn't an update package folder",
                path.display()
            );
        }
        let (files, skipped) = read_folder(&full)?;
        (folder_name(&full)?, files, skipped, full.parent().map(Path::to_path_buf))
    } else if full
        .file_name()
        .is_some_and(|n| n.eq_ignore_ascii_case(MANIFEST))
    {
        let dir = full.parent().ok_or_else(|| anyhow!("{} has no folder", path.display()))?;
        let (files, skipped) = read_folder(dir)?;
        (folder_name(dir)?, files, skipped, dir.parent().map(Path::to_path_buf))
    } else if is_tgz(&full) {
        let (name, files) = read_tgz(&full)?;
        (name, files, Vec::new(), full.parent().map(Path::to_path_buf))
    } else {
        bail!(
            "{} isn't an update package: give its folder, its {MANIFEST} or its .tgz",
            path.display()
        );
    };

    let mut files = files;
    files.sort_by(|a, b| {
        (a.name == MANIFEST)
            .cmp(&(b.name == MANIFEST))
            .then_with(|| a.name.cmp(&b.name))
    });
    check_manifest(&files)?;
    let build = beside.as_deref().and_then(find_build);
    Ok(Package {
        name,
        files,
        build,
        skipped,
    })
}

fn folder_name(dir: &Path) -> Result<String> {
    dir.file_name()
        .and_then(|n| n.to_str())
        .map(str::to_string)
        .ok_or_else(|| anyhow!("Can't tell the package's name from {}", dir.display()))
}

fn is_tgz(path: &Path) -> bool {
    let name = path
        .file_name()
        .and_then(|n| n.to_str())
        .unwrap_or("")
        .to_ascii_lowercase();
    name.ends_with(".tgz") || name.ends_with(".tar.gz")
}

fn read_folder(dir: &Path) -> Result<(Vec<PkgFile>, Vec<String>)> {
    let mut files = Vec::new();
    let mut skipped = Vec::new();
    for entry in fs::read_dir(dir).with_context(|| format!("Can't list {}", dir.display()))? {
        let entry = entry?;
        let name = entry.file_name().to_string_lossy().into_owned();
        if entry.file_type()?.is_dir() {
            skipped.push(format!("{name}/"));
            continue;
        }
        let data = fs::read(entry.path())
            .with_context(|| format!("Can't read {}", entry.path().display()))?;
        files.push(PkgFile::new(name, data));
    }
    Ok((files, skipped))
}

fn read_tgz(path: &Path) -> Result<(String, Vec<PkgFile>)> {
    let file = fs::File::open(path).with_context(|| format!("Can't open {}", path.display()))?;
    let mut archive = tar::Archive::new(flate2::read::GzDecoder::new(file));
    let mut top: Option<String> = None;
    let mut files = Vec::new();
    let bad = || anyhow!("{} isn't an update package .tgz", path.display());
    for entry in archive.entries().map_err(|_| bad())? {
        let mut entry = entry.map_err(|e| anyhow!("{}: {e}", bad()))?;
        let entry_path = entry.path()?.into_owned();
        let parts: Vec<String> = entry_path
            .components()
            .filter_map(|c| match c {
                std::path::Component::Normal(s) => Some(s.to_string_lossy().into_owned()),
                _ => None,
            })
            .collect();
        let Some(first) = parts.first() else { continue };
        match &top {
            None => top = Some(first.clone()),
            Some(t) if t != first => bail!(
                "{} holds more than one folder ({t}, {first}): it isn't one update package",
                path.display()
            ),
            _ => {}
        }
        if !entry.header().entry_type().is_file() {
            continue;
        }
        if parts.len() != 2 {
            bail!(
                "{} has {} inside a subfolder: update packages are flat",
                path.display(),
                entry_path.display()
            );
        }
        let mut data = Vec::with_capacity(entry.size() as usize);
        entry.read_to_end(&mut data)?;
        files.push(PkgFile::new(parts[1].clone(), data));
    }
    let top = top.ok_or_else(bad)?;
    if !files.iter().any(|f| f.name == MANIFEST) {
        bail!("{} has no {MANIFEST} in {top}/", path.display());
    }
    Ok((top, files))
}

/// Every file update.fuf names must be in the package
fn check_manifest(files: &[PkgFile]) -> Result<()> {
    let manifest = files
        .iter()
        .find(|f| f.name == MANIFEST)
        .ok_or_else(|| anyhow!("The package has no {MANIFEST}"))?;
    let text = String::from_utf8_lossy(&manifest.data);
    if !text.starts_with("Filetype: Flipper firmware upgrade configuration") {
        bail!("{MANIFEST} isn't a Flipper update manifest");
    }
    let mut named: Vec<&str> = Vec::new();
    for line in text.lines() {
        let Some((key, value)) = line.split_once(':') else {
            continue;
        };
        let value = value.trim();
        match key.trim() {
            "Loader" | "Firmware" | "Radio" | "Resources" | "Splashscreen" if !value.is_empty() => {
                named.push(value)
            }
            "Resources extra" => named.extend(value.split_whitespace()),
            _ => {}
        }
    }
    let missing: Vec<&str> = named
        .into_iter()
        .filter(|n| !files.iter().any(|f| f.name == *n))
        .collect();
    if !missing.is_empty() {
        bail!(
            "The package is incomplete: {MANIFEST} names {}, which it doesn't have",
            missing.join(", ")
        );
    }
    Ok(())
}

/// The newest *-full-*.json in the folder that holds the package
fn find_build(dir: &Path) -> Option<Build> {
    let mut best: Option<(SystemTime, PathBuf)> = None;
    for entry in fs::read_dir(dir).ok()?.flatten() {
        let name = entry.file_name().to_string_lossy().into_owned();
        if !(name.contains("-full-") && name.ends_with(".json")) {
            continue;
        }
        let modified = entry.metadata().and_then(|m| m.modified()).ok()?;
        if best.as_ref().is_none_or(|(t, _)| modified > *t) {
            best = Some((modified, entry.path()));
        }
    }
    let text = fs::read_to_string(best?.1).ok()?;
    Some(Build {
        version: json_str(&text, "firmware_version")?,
        commit: json_str(&text, "firmware_commit")?,
        date: json_str(&text, "firmware_build_date").unwrap_or_default(),
    })
}

/// A string value from fbt's small flat JSON
fn json_str(text: &str, key: &str) -> Option<String> {
    let quoted = format!("\"{key}\"");
    let at = text.find(&quoted)? + quoted.len();
    let rest = text[at..].trim_start().strip_prefix(':')?.trim_start();
    let rest = rest.strip_prefix('"')?;
    Some(rest[..rest.find('"')?].to_string())
}

/// The newest dist/<target>/<package>/ under the nearest folder above here that has one
pub fn find_default() -> Result<PathBuf> {
    let mut roots: Vec<PathBuf> = Vec::new();
    if let Ok(cwd) = env::current_dir() {
        roots.extend(cwd.ancestors().map(Path::to_path_buf));
    }
    if let Ok(exe) = env::current_exe() {
        roots.extend(exe.ancestors().skip(1).map(Path::to_path_buf));
    }
    for root in roots {
        let Ok(targets) = fs::read_dir(root.join("dist")) else {
            continue;
        };
        let mut best: Option<(SystemTime, PathBuf)> = None;
        for target in targets.flatten() {
            let Ok(packages) = fs::read_dir(target.path()) else {
                continue;
            };
            for package in packages.flatten() {
                let manifest = package.path().join(MANIFEST);
                if let Ok(modified) = fs::metadata(&manifest).and_then(|m| m.modified()) {
                    if best.as_ref().is_none_or(|(t, _)| modified > *t) {
                        best = Some((modified, package.path()));
                    }
                }
            }
        }
        if let Some((_, path)) = best {
            return Ok(path);
        }
    }
    bail!(
        "No package given, and none found in a dist/ folder above here.\n\
         Give the package's folder or .tgz, e.g. hadal-update dist/f7-C/f7-update-local"
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_fbt_json() {
        let text = "{\n    \"firmware_build_date\": \"24-11-2026\",\n    \"firmware_commit\": \"7fdcf919\",\n    \"firmware_target\": 7\n}";
        assert_eq!(json_str(text, "firmware_commit").as_deref(), Some("7fdcf919"));
        assert_eq!(json_str(text, "firmware_build_date").as_deref(), Some("24-11-2026"));
        assert_eq!(json_str(text, "firmware_version"), None);
    }

    #[test]
    fn manifest_names_must_exist() {
        let fuf = b"Filetype: Flipper firmware upgrade configuration\nVersion: 2\nFirmware: firmware.dfu\nResources extra: a.ths b.ths\n".to_vec();
        let mut files = vec![
            PkgFile::new(MANIFEST.into(), fuf),
            PkgFile::new("firmware.dfu".into(), vec![1]),
            PkgFile::new("a.ths".into(), vec![2]),
        ];
        let e = check_manifest(&files).unwrap_err().to_string();
        assert!(e.contains("b.ths"), "{e}");
        files.push(PkgFile::new("b.ths".into(), vec![3]));
        check_manifest(&files).unwrap();
    }

    #[test]
    fn md5_hex() {
        assert_eq!(PkgFile::new("x".into(), b"abc".to_vec()).md5, "900150983cd24fb0d6963f7d28e17f72");
    }

    /// The repo's own build, when there is one: the folder and the .tgz are the same package
    #[test]
    fn folder_and_tgz_agree() {
        let dist = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../dist/f7-C");
        let (dir, tgz) = (dist.join("f7-update-local"), dist.join("flipper-z-f7-update-local.tgz"));
        if !dir.is_dir() || !tgz.is_file() {
            eprintln!("no build in dist/f7-C, skipped");
            return;
        }
        let a = load(&dir).unwrap();
        let b = load(&tgz).unwrap();
        assert_eq!(a.name, b.name);
        assert_eq!(a.files.last().unwrap().name, MANIFEST);
        let sums = |p: &Package| {
            p.files
                .iter()
                .map(|f| (f.name.clone(), f.md5.clone()))
                .collect::<Vec<_>>()
        };
        assert_eq!(sums(&a), sums(&b));
    }
}
