//! hadal-update: installs a Hadal update package on a Flipper Zero over USB, sending only the files
//! that differ from the copy already in /ext/update/<package>/ on the Flipper.
//!
//! The protocol follows scripts/selfupdate.py and scripts/flipper/storage.py, over RPC (protobuf on
//! the same serial port, after `start_rpc_session`): writes in 4 KB pieces go faster than the
//! command line's, and one listing gives every file's size.

#[cfg(test)]
mod fake;
mod link;
mod package;
mod proto;
mod rpc;
mod ui;
mod updatelog;

use std::collections::HashMap;
use std::path::PathBuf;
use std::process::ExitCode;
use std::thread::sleep;
use std::time::{Duration, Instant};

use anyhow::{bail, Result};
use clap::Parser;

use link::{Link, Lost, Timeout};
use package::{Package, PkgFile, MANIFEST};
use rpc::Rpc;
use updatelog::Outcome;

const UPDATE_ROOT: &str = "/ext/update";
/// What an upload runs at, for the dry run's estimate (measured: ~120 KB/s, plus the checks)
const EST_SPEED: f64 = 110_000.0;

/// Install a Hadal update package on a Flipper Zero, sending only the files that changed
#[derive(Parser)]
#[command(version, about, long_about = None)]
struct Args {
    /// The package: its folder (dist/f7-C/f7-update-local), its update.fuf or its .tgz.
    /// Default: the newest package in the nearest dist/ above here
    package: Option<PathBuf>,

    /// The Flipper's serial port, e.g. COM3. Default: the Flipper found by its USB id
    #[arg(short, long)]
    port: Option<String>,

    /// Show what would be uploaded and removed, then stop: nothing is written or installed
    #[arg(short = 'n', long)]
    dry_run: bool,

    /// Upload every file, even those already on the Flipper
    #[arg(short, long)]
    force: bool,

    /// Upload only; don't install
    #[arg(long)]
    no_install: bool,

    /// Start the install, then leave without waiting for the Flipper to come back
    #[arg(long)]
    no_wait: bool,

    /// Keep old package files on the Flipper that this package doesn't have
    #[arg(long)]
    keep_extra: bool,

    /// The folder under /ext/update/ on the Flipper. Default: the package's own name
    #[arg(long, value_name = "NAME")]
    dir_name: Option<String>,

    /// File data per RPC write
    #[arg(long, value_name = "BYTES", default_value_t = 4096,
          value_parser = clap::value_parser!(u32).range(512..=16384))]
    chunk: u32,

    /// Print every RPC message
    #[arg(short, long)]
    verbose: bool,
}

fn main() -> ExitCode {
    let args = Args::parse();
    match run(&args) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            ui::fail(&format!("{e:#}"));
            ExitCode::FAILURE
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
enum Action {
    New,
    Changed,
    Forced,
    Same,
}

struct Step<'a> {
    file: &'a PkgFile,
    action: Action,
}

struct Plan<'a> {
    steps: Vec<Step<'a>>,
    /// Files in the Flipper's package folder that this package doesn't have
    extras: Vec<String>,
    folder_exists: bool,
}

impl Plan<'_> {
    fn uploads(&self) -> impl Iterator<Item = &PkgFile> {
        self.steps
            .iter()
            .filter(|s| s.action != Action::Same)
            .map(|s| s.file)
    }
}

fn run(args: &Args) -> Result<()> {
    let started = Instant::now();
    let path = match &args.package {
        Some(p) => p.clone(),
        None => package::find_default()?,
    };
    let pkg = package::load(&path)?;
    let dir_name = args.dir_name.clone().unwrap_or_else(|| pkg.name.clone());
    if dir_name.is_empty()
        || !dir_name.is_ascii()
        || dir_name.contains(['/', '\\'])
        || dir_name.starts_with('.')
    {
        bail!("\"{dir_name}\" can't be a folder name under {UPDATE_ROOT}/ (ASCII, no slashes)");
    }
    let remote_dir = format!("{UPDATE_ROOT}/{dir_name}");

    println!(
        "Package  {}: {} files, {}",
        pkg.name,
        pkg.files.len(),
        ui::size(pkg.total())
    );
    println!("         {}", path.display());
    if let Some(b) = &pkg.build {
        println!("         {} ({}), built {}", b.version, b.commit, b.date);
    }
    for s in &pkg.skipped {
        println!("         {s} isn't sent: packages are flat");
    }

    let port = link::pick_port(args.port.as_deref())?;
    let mut rpc = Rpc::start(connect(&port.name)?, args.verbose)?;
    let info = rpc.device_info()?;
    println!(
        "Flipper  {} on {}: {}",
        info.get("hardware_name").map_or("(no name)", String::as_str),
        port.name,
        running(&info)
    );

    let plan = compare(&mut rpc, &pkg, &remote_dir, args.force)?;
    let upload_bytes: u64 = plan.uploads().map(PkgFile::len).sum();
    let upload_count = plan.uploads().count();
    let removals = if args.keep_extra { 0 } else { plan.extras.len() };
    show_plan(&plan, &remote_dir, args.keep_extra);

    if args.dry_run {
        rpc.stop();
        println!();
        if upload_count == 0 && removals == 0 {
            println!("Dry run: the Flipper already has this package; nothing would be sent.");
        } else {
            println!(
                "Dry run: would send {upload_count} file{} ({}, about {}){}. Nothing was written.",
                plural(upload_count),
                ui::size(upload_bytes),
                ui::took(Duration::from_secs_f64(
                    upload_bytes as f64 / EST_SPEED + upload_count as f64 * 0.3
                )),
                if removals > 0 {
                    format!(" and remove {removals}")
                } else {
                    String::new()
                }
            );
        }
        if !args.no_install {
            println!("Without --dry-run it would then install the package and wait for the Flipper.");
        }
        return Ok(());
    }

    if upload_count > 0 {
        upload(&mut rpc, &plan, &remote_dir, args.chunk as usize, upload_bytes)?;
    } else {
        println!("\nNothing to send: the Flipper already has every file.");
    }
    if removals > 0 {
        for name in &plan.extras {
            rpc.delete(&format!("{remote_dir}/{name}"))?;
            println!("  removed {name}");
        }
    }

    if args.no_install {
        rpc.stop();
        println!("\nUploaded. Not installed (--no-install).");
        return Ok(());
    }
    install(rpc, &port.name, &pkg, &remote_dir, args, started)
}

fn plural(n: usize) -> &'static str {
    if n == 1 {
        ""
    } else {
        "s"
    }
}

/// "hadal-20261124 (7fdcf919), built 24-11-2026"
fn running(info: &std::collections::BTreeMap<String, String>) -> String {
    let get = |k: &str| info.get(k).map(String::as_str).unwrap_or("?");
    let dirty = if info.get("firmware_commit_dirty").is_some_and(|v| v == "true") {
        "+"
    } else {
        ""
    };
    format!(
        "{} ({}{dirty}), built {}",
        get("firmware_version"),
        get("firmware_commit"),
        get("firmware_build_date")
    )
}

/// Open the port and reach the command line, saying what to do when it doesn't answer
fn connect(port: &str) -> Result<Link> {
    match Link::connect(port, Duration::from_secs(5)) {
        Err(e) if e.is::<Timeout>() => bail!(
            "{e}.\nIf the Flipper shows its PIN screen, unlock it on the Flipper, then run this again."
        ),
        other => other,
    }
}

fn compare<'a>(rpc: &mut Rpc, pkg: &'a Package, remote_dir: &str, force: bool) -> Result<Plan<'a>> {
    let remote = rpc.list(remote_dir)?;
    let folder_exists = remote.is_some();
    let remote: HashMap<String, rpc::RemoteFile> = remote
        .unwrap_or_default()
        .into_iter()
        .map(|f| (f.name.clone(), f))
        .collect();

    let mut line = ui::Line::new();
    let mut steps = Vec::new();
    for file in &pkg.files {
        let action = match remote.get(&file.name) {
            None => Action::New,
            Some(r) if r.is_dir => {
                bail!("{remote_dir}/{} is a folder on the Flipper, not a file", file.name)
            }
            Some(_) if force => Action::Forced,
            Some(r) if u64::from(r.size) != file.len() => Action::Changed,
            Some(_) => {
                line.show(&format!("  comparing {}…", file.name), true);
                let theirs = rpc.md5(&format!("{remote_dir}/{}", file.name))?;
                if theirs.eq_ignore_ascii_case(&file.md5) {
                    Action::Same
                } else {
                    Action::Changed
                }
            }
        };
        steps.push(Step { file, action });
    }
    line.clear();

    let mut extras: Vec<String> = remote
        .values()
        .filter(|r| {
            !r.is_dir && left_by_a_package(&r.name) && !pkg.files.iter().any(|f| f.name == r.name)
        })
        .map(|r| r.name.clone())
        .collect();
    extras.sort();
    Ok(Plan {
        steps,
        extras,
        folder_exists,
    })
}

/// A file an older package could have left in the folder: one of the kinds packages are made of.
/// Not what the updater writes there itself: `.installed` (what the last install unpacked, so the
/// next one skips the archives that didn't change), `backup.tar` (/int's backup), any dotfile.
fn left_by_a_package(name: &str) -> bool {
    let lower = name.to_ascii_lowercase();
    !lower.starts_with('.')
        && lower != "backup.tar"
        && [".ths", ".dfu", ".bin", ".fuf"]
            .iter()
            .any(|ext| lower.ends_with(ext))
}

fn show_plan(plan: &Plan, remote_dir: &str, keep_extra: bool) {
    println!();
    if plan.folder_exists {
        println!("On the Flipper  {remote_dir}/");
    } else {
        println!("On the Flipper  {remote_dir}/ isn't there yet: every file goes");
    }
    let width = plan
        .steps
        .iter()
        .map(|s| s.file.name.len())
        .chain(plan.extras.iter().map(String::len))
        .max()
        .unwrap_or(0);
    for s in &plan.steps {
        let why = match s.action {
            Action::Same => continue,
            Action::New => "new",
            Action::Changed => "changed",
            Action::Forced => "forced",
        };
        println!(
            "  send    {:<width$}  {:>7}  {why}",
            s.file.name,
            ui::size(s.file.len())
        );
    }
    for name in &plan.extras {
        if keep_extra {
            println!("  keep    {name:<width$}  not in this package");
        } else {
            println!("  remove  {name:<width$}  not in this package");
        }
    }
    let same: Vec<&Step> = plan
        .steps
        .iter()
        .filter(|s| s.action == Action::Same)
        .collect();
    if !same.is_empty() {
        let bytes: u64 = same.iter().map(|s| s.file.len()).sum();
        println!(
            "  same    {} file{}, {}: already there",
            same.len(),
            plural(same.len()),
            ui::size(bytes)
        );
    }
}

fn upload(rpc: &mut Rpc, plan: &Plan, remote_dir: &str, chunk: usize, total: u64) -> Result<()> {
    let count = plan.uploads().count();
    println!("\nSending {count} file{}, {}", plural(count), ui::size(total));
    rpc.mkdir(UPDATE_ROOT)?;
    rpc.mkdir(remote_dir)?;

    let width = plan.uploads().map(|f| f.name.len()).max().unwrap_or(0);
    let mut line = ui::Line::new();
    let all = Instant::now();
    let mut done: u64 = 0;
    let mut send_time = Duration::ZERO;
    for file in plan.uploads() {
        let path = format!("{remote_dir}/{}", file.name);
        let mut attempt = 1;
        loop {
            let t = Instant::now();
            rpc.write_file(&path, &file.data, chunk, |sent| {
                let sent = sent as u64;
                let overall = done + sent;
                let rate = overall as f64 / (send_time + t.elapsed()).as_secs_f64().max(0.001);
                let left = Duration::from_secs_f64(total.saturating_sub(overall) as f64 / rate.max(1.0));
                line.show(
                    &format!(
                        "  {:<width$}  {}  {:>3}%  {} of {}  {}  {} left",
                        file.name,
                        ui::bar(sent as f64 / file.len().max(1) as f64, 20),
                        sent * 100 / file.len().max(1),
                        ui::size(overall),
                        ui::size(total),
                        ui::speed(overall, send_time + t.elapsed()),
                        ui::clock(left),
                    ),
                    false,
                );
            })?;
            let sent_in = t.elapsed();
            line.show(&format!("  {:<width$}  checking…", file.name), true);
            let theirs = rpc.md5(&path)?;
            line.clear();
            if theirs.eq_ignore_ascii_case(&file.md5) {
                send_time += sent_in;
                done += file.len();
                println!(
                    "  ✓ {:<width$}  {:>7}  {:>6}  {}",
                    file.name,
                    ui::size(file.len()),
                    ui::took(sent_in),
                    ui::speed(file.len(), sent_in)
                );
                break;
            }
            if attempt == 2 {
                bail!(
                    "{} arrived different twice (MD5 on the Flipper {theirs}, here {}).\n\
                     The SD card may be failing: check it, or try another card.",
                    file.name,
                    file.md5
                );
            }
            println!("  ! {} arrived different; sending it again", file.name);
            attempt += 1;
        }
    }
    println!(
        "  Sent {} in {} ({} while sending), each file checked on the Flipper",
        ui::size(total),
        ui::took(all.elapsed()),
        ui::speed(total, send_time)
    );
    Ok(())
}

fn install(
    mut rpc: Rpc,
    port: &str,
    pkg: &Package,
    remote_dir: &str,
    args: &Args,
    started: Instant,
) -> Result<()> {
    println!("\nInstalling");
    let log_offset = u64::from(rpc.stat_size(updatelog::LOG_PATH)?.unwrap_or(0));

    // The firmware's own `update install` won't run while an app is open; neither does this
    if rpc.app_open()? {
        println!("  An app is open on the Flipper. Close it to go on (Ctrl+C stops here).");
        let t = Instant::now();
        loop {
            sleep(Duration::from_secs(1));
            if !rpc.app_open()? {
                break;
            }
            if t.elapsed() > Duration::from_secs(120) {
                rpc.stop();
                bail!(
                    "The app is still open. The package is on the Flipper: run this again once \
                     it's closed, and nothing will need sending."
                );
            }
        }
    }

    let manifest = format!("{remote_dir}/{MANIFEST}");
    let code = rpc.prepare_update(&manifest)?;
    if code != 0 {
        rpc.stop();
        bail!(
            "The Flipper won't install {manifest}: {}",
            proto::prepare_result_text(code)
        );
    }
    println!("  Package checked by the Flipper; restarting it into the updater");
    rpc.reboot_to_update()?;
    drop(rpc); // the port closes; the Flipper restarts

    if args.no_wait {
        println!("  The Flipper is installing it now (--no-wait: not watching).");
        return Ok(());
    }
    wait_for_result(port, log_offset, pkg, started)
}

/// After the restart: wait until the Flipper is back from the updater, then read what it did
fn wait_for_result(port: &str, log_offset: u64, pkg: &Package, started: Instant) -> Result<()> {
    let t0 = Instant::now();
    let mut line = ui::Line::new();
    let mut gone_once = false;
    let mut pin_hint = false;
    let status = |line: &mut ui::Line, what: &str| {
        line.show(&format!("  {}  {what}", ui::clock(t0.elapsed())), false);
    };
    let mut port = port.to_string();

    loop {
        if t0.elapsed() > Duration::from_secs(15 * 60) {
            line.clear();
            bail!("The Flipper hasn't come back in 15 minutes. Look at its screen.");
        }
        if gone_once && !link::is_present(&port) {
            // Windows may give it another COM number: follow the only Flipper there is
            if let [only] = link::flipper_ports().as_slice() {
                line.clear();
                println!("  The Flipper came back as {}", only.name);
                port = only.name.clone();
            }
        }
        if !link::is_present(&port) {
            gone_once = true;
            if !pin_hint && t0.elapsed() > Duration::from_secs(150) {
                line.clear();
                println!("  Still away. If the Flipper asks for its PIN, enter it on the Flipper.");
                pin_hint = true;
            }
            status(&mut line, "the Flipper is off USB while the updater writes the firmware");
            sleep(Duration::from_millis(400));
            continue;
        }
        if !gone_once && t0.elapsed() < Duration::from_secs(20) {
            status(&mut line, "restarting into the updater");
            sleep(Duration::from_millis(300));
            continue;
        }

        let mut link = match Link::connect(&port, Duration::from_secs(4)) {
            Ok(l) => l,
            Err(e) => {
                if e.is::<Timeout>() && !pin_hint && gone_once {
                    line.clear();
                    println!(
                        "  The Flipper is on USB but not answering yet. If it asks for its PIN, \
                         enter it on the Flipper."
                    );
                    pin_hint = true;
                }
                status(&mut line, "waiting for the Flipper's command line");
                sleep(Duration::from_secs(1));
                continue;
            }
        };

        // Back on USB: is the updater app still at work (before or after the firmware write)?
        // It's back once two answers in a row, 1.5 s apart, say the updater isn't running: the
        // first could come from a start that hasn't opened the updater yet
        let mut back = false;
        let mut idle_answers = 0;
        loop {
            match link.cli("loader info", Duration::from_secs(3)) {
                Ok(out) if out.contains("\"UpdaterApp\"") => {
                    gone_once = true;
                    idle_answers = 0;
                    status(&mut line, "the updater is at work on the Flipper");
                }
                Ok(_) => {
                    idle_answers += 1;
                    if idle_answers == 2 {
                        back = true;
                        break;
                    }
                    status(&mut line, "the Flipper is back; checking");
                }
                Err(e) if e.is::<Lost>() => break,
                Err(_) => break, // no answer: it may be restarting, look again
            }
            sleep(Duration::from_millis(1500));
        }
        if !back {
            continue; // it restarted (or stopped answering): look again
        }
        line.clear();
        return report(link, log_offset, pkg, t0.elapsed(), started);
    }
}

fn report(link: Link, log_offset: u64, pkg: &Package, waited: Duration, started: Instant) -> Result<()> {
    let mut rpc = Rpc::start(link, false)?;
    let info = rpc.device_info()?;
    let log = match rpc.read_file(updatelog::LOG_PATH) {
        Ok(l) => l,
        Err(_) => Vec::new(),
    };
    rpc.stop();
    let report = updatelog::parse(updatelog::since(&log, log_offset));
    let now = running(&info);
    let expected = pkg.build.as_ref().map(|b| {
        info.get("firmware_commit")
            .is_some_and(|c| c.starts_with(&b.commit) || b.commit.starts_with(c.as_str()))
            && info
                .get("firmware_version")
                .is_some_and(|v| *v == b.version)
    });

    match report.outcome {
        Outcome::Done => {
            println!("  Back after {}: the updater finished", ui::clock(waited));
            println!("\n✓ Installed. The Flipper runs {now}");
            if expected == Some(false) {
                let b = pkg.build.as_ref().unwrap();
                println!(
                    "  (the package's build info says {} ({}); it may be from another build)",
                    b.version, b.commit
                );
            }
            println!("  {} in all", ui::clock(started.elapsed()));
            Ok(())
        }
        Outcome::Failed(reason) => {
            println!("  The updater's log:");
            for l in &report.lines {
                println!("    {l}");
            }
            bail!("The update failed: {reason}.\nThe Flipper runs {now}")
        }
        Outcome::Unknown => {
            println!("  Back after {}. The Flipper runs {now}", ui::clock(waited));
            for l in &report.lines {
                println!("    {l}");
            }
            match expected {
                Some(true) => {
                    println!("\n✓ That's this package's build (the update log has no word on it).");
                    Ok(())
                }
                _ => bail!(
                    "The Flipper is back, but its update log ({}) says nothing about this update.\n\
                     Look at the Flipper's screen, or run this again.",
                    updatelog::LOG_PATH
                ),
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fake::{Card, Fake};
    use std::cell::RefCell;
    use std::fs;
    use std::path::Path;
    use std::rc::Rc;

    const DIR: &str = "/ext/update/f7-update-local";

    fn make_package(root: &Path) -> Package {
        let dir = root.join("f7-update-local");
        fs::create_dir_all(&dir).unwrap();
        let firmware: Vec<u8> = (0..9000u32).map(|i| (i * 7) as u8).collect();
        fs::write(dir.join("firmware.dfu"), firmware).unwrap();
        fs::write(dir.join("res-a.ths"), b"resources").unwrap();
        fs::write(
            dir.join(MANIFEST),
            "Filetype: Flipper firmware upgrade configuration
Version: 2
             Firmware: firmware.dfu
Resources extra: res-a.ths
",
        )
        .unwrap();
        package::load(&dir).unwrap()
    }

    fn session(card: &Rc<RefCell<Card>>) -> Rpc {
        let mut link = Link::over(Box::new(Fake::new(card.clone())), "FAKE");
        link.sync_cli(Duration::from_secs(1)).unwrap();
        assert_eq!(
            link.cli("loader info", Duration::from_secs(1)).unwrap(),
            "No application is running"
        );
        Rpc::start(link, false).unwrap()
    }

    fn scratch(name: &str) -> PathBuf {
        let p = std::env::temp_dir().join(format!("hadal-update-{name}-{}", std::process::id()));
        let _ = fs::remove_dir_all(&p);
        p
    }

    #[test]
    fn sends_only_what_changed() {
        let root = scratch("changed");
        let pkg = make_package(&root);
        let card = Rc::new(RefCell::new(Card::default()));
        {
            let mut c = card.borrow_mut();
            c.dirs.insert(UPDATE_ROOT.into());
            c.dirs.insert(DIR.into());
            // the same resources, a firmware of the same size but older, a file the package dropped
            c.files.insert(format!("{DIR}/res-a.ths"), b"resources".to_vec());
            c.files.insert(format!("{DIR}/firmware.dfu"), vec![0; 9000]);
            c.files.insert(format!("{DIR}/res-old.ths"), vec![1, 2, 3]);
            // what the updater itself keeps there
            c.files.insert(format!("{DIR}/.installed"), b"M 00\n".to_vec());
            c.files.insert(format!("{DIR}/backup.tar"), vec![0; 512]);
        }
        let mut rpc = session(&card);
        assert_eq!(rpc.device_info().unwrap()["hardware_name"], "Fakey");

        let plan = compare(&mut rpc, &pkg, DIR, false).unwrap();
        let actions: Vec<(&str, Action)> = plan
            .steps
            .iter()
            .map(|s| (s.file.name.as_str(), s.action))
            .collect();
        assert_eq!(
            actions,
            [
                ("firmware.dfu", Action::Changed),
                ("res-a.ths", Action::Same),
                (MANIFEST, Action::New)
            ]
        );
        assert_eq!(plan.extras, ["res-old.ths"]);

        let total = plan.uploads().map(PkgFile::len).sum();
        upload(&mut rpc, &plan, DIR, 4096, total).unwrap();
        for f in &pkg.files {
            assert_eq!(card.borrow().files[&format!("{DIR}/{}", f.name)], f.data, "{}", f.name);
        }
        let manifest_len = pkg.files.last().unwrap().data.len();
        assert_eq!(card.borrow().pieces, [4096, 4096, 808, manifest_len]);

        rpc.delete(&format!("{DIR}/res-old.ths")).unwrap();
        assert!(!card.borrow().files.contains_key(&format!("{DIR}/res-old.ths")));

        // a second look finds nothing to send
        let again = compare(&mut rpc, &pkg, DIR, false).unwrap();
        assert_eq!(again.uploads().count(), 0);
        assert!(again.extras.is_empty());

        assert!(!rpc.app_open().unwrap());
        assert_eq!(rpc.prepare_update(&format!("{DIR}/{MANIFEST}")).unwrap(), 0);
        assert_eq!(rpc.prepare_update("/ext/update/nope/update.fuf").unwrap(), 2);
        rpc.reboot_to_update().unwrap();
        assert!(card.borrow().rebooted);
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn only_package_files_are_removed() {
        for name in ["res-apps-GPIO-69b251.ths", "firmware.dfu", "radio.bin", "update.fuf"] {
            assert!(left_by_a_package(name), "{name}");
        }
        for name in [".installed", ".fupdate", "backup.tar", "BACKUP.TAR", "notes.txt", ".x.ths"] {
            assert!(!left_by_a_package(name), "{name}");
        }
    }

    #[test]
    fn a_new_folder_gets_everything() {
        let root = scratch("new");
        let pkg = make_package(&root);
        let card = Rc::new(RefCell::new(Card::default()));
        let mut rpc = session(&card);
        let plan = compare(&mut rpc, &pkg, DIR, false).unwrap();
        assert!(!plan.folder_exists);
        assert!(plan.steps.iter().all(|s| s.action == Action::New));
        upload(&mut rpc, &plan, DIR, 4096, pkg.total()).unwrap();
        assert!(card.borrow().dirs.contains(DIR));
        assert_eq!(card.borrow().files.len(), 3);
        assert_eq!(rpc.stat_size(&format!("{DIR}/res-a.ths")).unwrap(), Some(9));
        assert_eq!(rpc.stat_size(updatelog::LOG_PATH).unwrap(), None);
        rpc.stop();
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn force_sends_files_that_match() {
        let root = scratch("force");
        let pkg = make_package(&root);
        let card = Rc::new(RefCell::new(Card::default()));
        card.borrow_mut().dirs.insert(DIR.into());
        card.borrow_mut()
            .files
            .insert(format!("{DIR}/res-a.ths"), b"resources".to_vec());
        let mut rpc = session(&card);
        let plan = compare(&mut rpc, &pkg, DIR, true).unwrap();
        assert_eq!(plan.steps[1].action, Action::Forced);
        let _ = fs::remove_dir_all(&root);
    }
}
