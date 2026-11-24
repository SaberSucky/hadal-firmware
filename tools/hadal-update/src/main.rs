//! hadal-update: installs a Hadal update package on a Flipper Zero over USB, sending only the files
//! that differ from the copy already in /ext/update/<package>/ on the Flipper.
//!
//! The protocol follows scripts/selfupdate.py and scripts/flipper/storage.py, over RPC (protobuf on
//! the same serial port, after `start_rpc_session`): writes in 4 KB pieces go faster than the
//! command line's, one listing gives every file's size, and small requests can overlap.

mod cache;
#[cfg(test)]
mod fake;
mod link;
mod package;
mod proto;
mod rpc;
mod ui;
mod updatelog;

use std::collections::{BTreeMap, HashMap};
use std::path::PathBuf;
use std::process::ExitCode;
use std::thread::sleep;
use std::time::{Duration, Instant};

use anyhow::{bail, Result};
use clap::Parser;

use cache::{Cache, Seen};
use link::{Link, Timeout};
use package::{Package, PkgFile, MANIFEST};
use rpc::Rpc;
use updatelog::Outcome;

const UPDATE_ROOT: &str = "/ext/update";
/// What an upload runs at, for the dry run's estimate. Measured on a Flipper: 990 KB in 6.9 s,
/// firmware.dfu's and updater.bin's MD5s left to the updater's CRC checks
const EST_SPEED: f64 = 143_000.0;

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

    /// Check every sent file's MD5 on the Flipper, also those the updater checks by CRC itself
    /// (firmware.dfu, updater.bin, radio.bin)
    #[arg(long)]
    verify: bool,

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

    /// Bytes handed to the USB driver per write (0: each message on its own, as measured)
    #[arg(long, value_name = "BYTES", default_value_t = 0, hide = true)]
    batch: u32,

    /// Time writes to a scratch file on the Flipper (then deleted) at a few sizes, and stop
    #[arg(long, hide = true)]
    bench: bool,

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
    /// Old package files in the Flipper's folder that this package doesn't have
    extras: Vec<String>,
    folder_exists: bool,
    /// How many files the Flipper hashed to compare (the rest: their size, or the cache)
    hashed: usize,
}

impl Plan<'_> {
    fn uploads(&self) -> impl Iterator<Item = &PkgFile> {
        self.steps
            .iter()
            .filter(|s| s.action != Action::Same)
            .map(|s| s.file)
    }
}

/// Files whose MD5 isn't checked after sending (the updater checks their CRC): recorded in the
/// cache once the install has gone through
type Unchecked = Vec<(String, Seen)>;

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
    if args.bench {
        return bench(rpc, &pkg);
    }

    let mut cache = Cache::load(info.get("hardware_uid").map_or("", String::as_str));
    let compare_started = Instant::now();
    let plan = compare(&mut rpc, &pkg, &remote_dir, args.force, &mut cache)?;
    cache.save();
    let upload_bytes: u64 = plan.uploads().map(PkgFile::len).sum();
    let upload_count = plan.uploads().count();
    let removals = if args.keep_extra { 0 } else { plan.extras.len() };
    show_plan(&plan, &remote_dir, args.keep_extra, compare_started.elapsed());

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
                    upload_bytes as f64 / EST_SPEED + upload_count as f64 * 0.05
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

    let mut unchecked = Unchecked::new();
    if upload_count > 0 {
        unchecked = upload(&mut rpc, &plan, &pkg, &remote_dir, args, upload_bytes, &mut cache)?;
    } else {
        println!("\nNothing to send: the Flipper already has every file.");
    }
    if removals > 0 {
        let paths: Vec<String> = plan
            .extras
            .iter()
            .map(|name| format!("{remote_dir}/{name}"))
            .collect();
        rpc.delete_all(&paths)?;
        for p in &paths {
            cache.forget(p);
        }
        println!("  removed {}", plan.extras.join(", "));
    }
    cache.save();

    if args.no_install {
        rpc.stop();
        println!("\nUploaded. Not installed (--no-install).");
        return Ok(());
    }
    install(rpc, &port.name, &pkg, &remote_dir, args, started, cache, unchecked)
}

fn plural(n: usize) -> &'static str {
    if n == 1 {
        ""
    } else {
        "s"
    }
}

/// "hadal-20261124 (7fdcf919), built 24-11-2026"
fn running(info: &BTreeMap<String, String>) -> String {
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

fn compare<'a>(
    rpc: &mut Rpc,
    pkg: &'a Package,
    remote_dir: &str,
    force: bool,
    cache: &mut Cache,
) -> Result<Plan<'a>> {
    let remote = rpc.list(remote_dir)?;
    let folder_exists = remote.is_some();
    let remote: HashMap<String, rpc::RemoteFile> = remote
        .unwrap_or_default()
        .into_iter()
        .map(|f| (f.name.clone(), f))
        .collect();

    let mut actions: Vec<Option<Action>> = Vec::new();
    for file in &pkg.files {
        actions.push(match remote.get(&file.name) {
            None => Some(Action::New),
            Some(r) if r.is_dir => {
                bail!("{remote_dir}/{} is a folder on the Flipper, not a file", file.name)
            }
            Some(_) if force => Some(Action::Forced),
            Some(r) if u64::from(r.size) != file.len() => Some(Action::Changed),
            Some(_) => None, // the same size: the MD5 decides
        });
    }

    // A same-size file the cache saw with this timestamp has the MD5 it noted then; the Flipper
    // hashes the others. Timestamps are cheap: all of them in one round trip
    let undecided: Vec<usize> = (0..pkg.files.len()).filter(|&i| actions[i].is_none()).collect();
    let paths: Vec<String> = undecided
        .iter()
        .map(|&i| format!("{remote_dir}/{}", pkg.files[i].name))
        .collect();
    let stamps = if paths.is_empty() {
        Vec::new()
    } else {
        rpc.timestamps(&paths)?
    };
    let mut theirs: Vec<Option<String>> = vec![None; undecided.len()];
    let mut to_hash: Vec<usize> = Vec::new();
    for (k, &i) in undecided.iter().enumerate() {
        let size = pkg.files[i].len() as u32;
        match stamps[k].and_then(|ts| cache.md5(&paths[k], size, ts)) {
            Some(md5) => theirs[k] = Some(md5.to_string()),
            None => to_hash.push(k),
        }
    }
    if !to_hash.is_empty() {
        let mut line = ui::Line::new();
        line.show(
            &format!("  {} file{} to hash on the Flipper…", to_hash.len(), plural(to_hash.len())),
            true,
        );
        let hash_paths: Vec<String> = to_hash.iter().map(|&k| paths[k].clone()).collect();
        let sums = rpc.md5s(&hash_paths)?;
        line.clear();
        for (&k, md5) in to_hash.iter().zip(sums) {
            if let Some(timestamp) = stamps[k] {
                let size = pkg.files[undecided[k]].len() as u32;
                cache.set(
                    &paths[k],
                    Seen {
                        size,
                        timestamp,
                        md5: md5.clone(),
                    },
                );
            }
            theirs[k] = Some(md5);
        }
    }
    for (k, &i) in undecided.iter().enumerate() {
        let same = theirs[k]
            .as_deref()
            .is_some_and(|m| m.eq_ignore_ascii_case(&pkg.files[i].md5));
        actions[i] = Some(if same { Action::Same } else { Action::Changed });
    }

    let steps = pkg
        .files
        .iter()
        .zip(actions)
        .map(|(file, action)| Step {
            file,
            action: action.expect("every file decided"),
        })
        .collect();
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
        hashed: to_hash.len(),
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

fn show_plan(plan: &Plan, remote_dir: &str, keep_extra: bool, took: Duration) {
    println!();
    if plan.folder_exists {
        println!(
            "On the Flipper  {remote_dir}/  (compared in {}{})",
            ui::took(took),
            match plan.hashed {
                0 => String::new(),
                n => format!(", {n} file{} hashed there", plural(n)),
            }
        );
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

fn upload(
    rpc: &mut Rpc,
    plan: &Plan,
    pkg: &Package,
    remote_dir: &str,
    args: &Args,
    total: u64,
    cache: &mut Cache,
) -> Result<Unchecked> {
    let count = plan.uploads().count();
    println!("\nSending {count} file{}, {}", plural(count), ui::size(total));
    rpc.mkdir(UPDATE_ROOT)?;
    rpc.mkdir(remote_dir)?;

    let (chunk, batch) = (args.chunk as usize, args.batch as usize);
    let width = plan.uploads().map(|f| f.name.len()).max().unwrap_or(0);
    let mut line = ui::Line::new();
    let all = Instant::now();
    let mut done: u64 = 0;
    let mut send_time = Duration::ZERO;
    let mut sent: Vec<(String, &PkgFile, bool)> = Vec::new();
    for file in plan.uploads() {
        let path = format!("{remote_dir}/{}", file.name);
        let check = args.verify || !pkg.checked_by_updater.contains(&file.name);
        // until it's there whole, the cache knows nothing about it
        cache.forget(&path);
        let mut attempt = 1;
        loop {
            let t = Instant::now();
            rpc.write_file(&path, &file.data, chunk, batch, |sent| {
                let sent = sent as u64;
                let overall = done + sent;
                let elapsed = send_time + t.elapsed();
                let rate = overall as f64 / elapsed.as_secs_f64().max(0.001);
                let left = Duration::from_secs_f64(total.saturating_sub(overall) as f64 / rate.max(1.0));
                line.show(
                    &format!(
                        "  {:<width$}  {}  {:>3}%  {} of {}  {}  {} left",
                        file.name,
                        ui::bar(sent as f64 / file.len().max(1) as f64, 20),
                        sent * 100 / file.len().max(1),
                        ui::size(overall),
                        ui::size(total),
                        ui::speed(overall, elapsed),
                        ui::clock(left),
                    ),
                    false,
                );
            })?;
            let sent_in = t.elapsed();
            let theirs = if check {
                line.show(&format!("  {:<width$}  checking…", file.name), true);
                Some(rpc.md5(&path)?)
            } else {
                None
            };
            line.clear();
            if theirs.as_deref().is_none_or(|m| m.eq_ignore_ascii_case(&file.md5)) {
                send_time += sent_in;
                done += file.len();
                println!(
                    "  ✓ {:<width$}  {:>7}  {:>6}  {}{}",
                    file.name,
                    ui::size(file.len()),
                    ui::took(sent_in),
                    ui::speed(file.len(), sent_in),
                    if check { "" } else { "  (its CRC is checked by the updater)" }
                );
                break;
            }
            if attempt == 2 {
                bail!(
                    "{} arrived different twice (MD5 on the Flipper {}, here {}).\n\
                     The SD card may be failing: check it, or try another card.",
                    file.name,
                    theirs.unwrap_or_default(),
                    file.md5
                );
            }
            println!("  ! {} arrived different; sending it again", file.name);
            attempt += 1;
        }
        sent.push((path, file, check));
    }
    println!(
        "  Sent {} in {} ({} while sending)",
        ui::size(total),
        ui::took(all.elapsed()),
        ui::speed(total, send_time)
    );

    // What the card now has, for the cache: the files checked go in now, the others once the
    // updater has checked them
    let paths: Vec<String> = sent.iter().map(|(p, _, _)| p.clone()).collect();
    let stamps = rpc.timestamps(&paths)?;
    let mut unchecked = Unchecked::new();
    for ((path, file, checked), stamp) in sent.into_iter().zip(stamps) {
        let Some(timestamp) = stamp else { continue };
        let seen = Seen {
            size: file.len() as u32,
            timestamp,
            md5: file.md5.clone(),
        };
        if checked {
            cache.set(&path, seen);
        } else {
            unchecked.push((path, seen));
        }
    }
    Ok(unchecked)
}

#[allow(clippy::too_many_arguments)]
fn install(
    mut rpc: Rpc,
    port: &str,
    pkg: &Package,
    remote_dir: &str,
    args: &Args,
    started: Instant,
    mut cache: Cache,
    unchecked: Unchecked,
) -> Result<()> {
    println!("\nInstalling");
    let log_offset = rpc.stat_size(updatelog::LOG_PATH)?.unwrap_or(0);

    // The firmware's own `update install` won't run while an app is open; neither does this
    if rpc.app_open()? {
        println!("  An app is open on the Flipper. Close it to go on (Ctrl+C stops here).");
        let t = Instant::now();
        loop {
            sleep(Duration::from_millis(500));
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
    let (rpc, report, timeline) = wait_for_result(port, log_offset)?;
    finish(rpc, report, timeline, pkg, started, &mut cache, unchecked)
}

/// When things happened while the Flipper updated, from the restart on
struct Timeline {
    start: Instant,
    events: Vec<(Duration, &'static str)>,
}

impl Timeline {
    fn note(&mut self, what: &'static str) {
        if self.events.last().map(|e| e.1) != Some(what) {
            self.events.push((self.start.elapsed(), what));
        }
    }

    fn show(&self) -> String {
        self.events
            .iter()
            .map(|(t, what)| format!("{what} {:.1} s", t.as_secs_f64()))
            .collect::<Vec<_>>()
            .join(", ")
    }
}

/// After the restart: wait until the Flipper is back from the updater; then its RPC session,
/// what the update log says, and when things happened
fn wait_for_result(port: &str, log_offset: u32) -> Result<(Rpc, updatelog::Report, Timeline)> {
    let mut timeline = Timeline {
        start: Instant::now(),
        events: Vec::new(),
    };
    let t0 = timeline.start;
    let mut line = ui::Line::new();
    let mut port = port.to_string();
    let mut gone_once = false;
    let mut pin_hint = false;
    let mut undecided_since: Option<Instant> = None;
    let status = |line: &mut ui::Line, what: &str| {
        line.show(&format!("  {}  {what}", ui::clock(t0.elapsed())), false);
    };

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
            timeline.note("off USB");
            if !pin_hint && t0.elapsed() > Duration::from_secs(150) {
                line.clear();
                println!("  Still away. If the Flipper asks for its PIN, enter it on the Flipper.");
                pin_hint = true;
            }
            status(&mut line, "the Flipper is restarting, or writing the firmware off USB");
            sleep(Duration::from_millis(100));
            continue;
        }
        if !gone_once && t0.elapsed() < Duration::from_secs(20) {
            status(&mut line, "restarting into the updater");
            sleep(Duration::from_millis(100));
            continue;
        }

        timeline.note("on USB");
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
                sleep(Duration::from_millis(200));
                continue;
            }
        };

        // The updater app at work (before the firmware write, or after it): it restarts the
        // Flipper when it's done, so keep the port open until that
        let updater_open = |link: &mut Link| -> Result<bool> {
            Ok(link
                .cli("loader info", Duration::from_secs(3))?
                .contains("\"UpdaterApp\""))
        };
        match updater_open(&mut link) {
            Ok(true) => {
                gone_once = true;
                timeline.note("updater at work");
                status(&mut line, "the updater is at work on the Flipper");
                let _ = link.hold_until_gone(Duration::from_secs(600));
                continue;
            }
            Ok(false) => {}
            Err(_) => {
                sleep(Duration::from_millis(200));
                continue;
            }
        }
        // Once more, a moment later, before a session takes RAM: a start that hadn't opened the
        // updater yet would show it now
        sleep(Duration::from_millis(250));
        match updater_open(&mut link) {
            Ok(false) => {}
            _ => continue,
        }

        let mut rpc = match Rpc::start(link, false) {
            Ok(r) => r,
            Err(_) => {
                sleep(Duration::from_millis(300));
                continue;
            }
        };
        let report = match read_new_log(&mut rpc, log_offset) {
            Ok(r) => r,
            Err(_) => {
                sleep(Duration::from_millis(300));
                continue;
            }
        };
        // The log decides; if it says nothing for a few seconds, the caller tells the user so
        let first_undecided = *undecided_since.get_or_insert_with(Instant::now);
        if report.outcome != Outcome::Unknown || first_undecided.elapsed() > Duration::from_secs(6)
        {
            timeline.note("back");
            line.clear();
            return Ok((rpc, report, timeline));
        }
        rpc.stop();
        status(&mut line, "the Flipper is up; its update log has no result yet");
        sleep(Duration::from_millis(500));
    }
}

/// What the update log gained since `offset` (all of it if it was started over since)
fn read_new_log(rpc: &mut Rpc, offset: u32) -> Result<updatelog::Report> {
    let new = match rpc.stat_size(updatelog::LOG_PATH)? {
        None => Vec::new(),
        Some(size) if size < offset => rpc.read_file_from(updatelog::LOG_PATH, 0)?,
        Some(_) => rpc.read_file_from(updatelog::LOG_PATH, offset)?,
    };
    Ok(updatelog::parse(&new))
}

fn finish(
    mut rpc: Rpc,
    report: updatelog::Report,
    timeline: Timeline,
    pkg: &Package,
    started: Instant,
    cache: &mut Cache,
    unchecked: Unchecked,
) -> Result<()> {
    let info = rpc.device_info()?;
    rpc.stop();
    let now = running(&info);
    let expected = pkg.build.as_ref().map(|b| {
        info.get("firmware_commit")
            .is_some_and(|c| c.starts_with(&b.commit) || b.commit.starts_with(c.as_str()))
            && info
                .get("firmware_version")
                .is_some_and(|v| *v == b.version)
    });
    println!("  {}", timeline.show());

    match report.outcome {
        Outcome::Done => {
            // the updater has checked the files it checks: they're as sent
            for (path, seen) in unchecked {
                cache.set(&path, seen);
            }
            cache.save();
            println!("\n✓ Installed. The Flipper runs {now}");
            if expected == Some(false) {
                let b = pkg.build.as_ref().unwrap();
                println!(
                    "  (the package's build info says {} ({}); it may be from another build)",
                    b.version, b.commit
                );
            }
            println!("  {} in all", ui::took(started.elapsed()));
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
            println!("  The Flipper is back and runs {now}");
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

/// Write the package's biggest file to a scratch file a few ways, timing each; then delete it
fn bench(mut rpc: Rpc, pkg: &Package) -> Result<()> {
    let file = pkg
        .files
        .iter()
        .max_by_key(|f| f.len())
        .expect("a package has files");
    let path = format!("{UPDATE_ROOT}/.hadal-update-bench");
    rpc.mkdir(UPDATE_ROOT)?;
    println!("\nWriting {} ({}) to {path}", file.name, ui::size(file.len()));
    for (chunk, batch) in [(4096, 0), (8192, 0), (4096, 32768), (8192, 32768)] {
        let t = Instant::now();
        rpc.write_file(&path, &file.data, chunk, batch, |_| {})?;
        let took = t.elapsed();
        println!(
            "  {chunk:>5}-byte messages, {:>14}: {:>6}  {}",
            if batch == 0 {
                "one per write".to_string()
            } else {
                format!("{batch}-byte writes")
            },
            ui::took(took),
            ui::speed(file.len(), took)
        );
    }
    let t = Instant::now();
    let md5 = rpc.md5(&path)?;
    println!(
        "  MD5 on the Flipper: {} ({})",
        ui::took(t.elapsed()),
        if md5 == file.md5 { "matches" } else { "DIFFERS" }
    );
    rpc.delete(&path)?;
    rpc.stop();
    Ok(())
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
            "Filetype: Flipper firmware upgrade configuration\nVersion: 2\n\
             Firmware: firmware.dfu\nResources extra: res-a.ths\n",
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

    fn args(extra: &[&str]) -> Args {
        Args::parse_from(std::iter::once("hadal-update").chain(extra.iter().copied()))
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
            c.put(&format!("{DIR}/res-a.ths"), b"resources");
            c.put(&format!("{DIR}/firmware.dfu"), &[0; 9000]);
            c.put(&format!("{DIR}/res-old.ths"), &[1, 2, 3]);
            // what the updater itself keeps there
            c.put(&format!("{DIR}/.installed"), b"M 00\n");
            c.put(&format!("{DIR}/backup.tar"), &[0; 512]);
        }
        let mut rpc = session(&card);
        assert_eq!(rpc.device_info().unwrap()["hardware_name"], "Fakey");

        let mut cache = Cache::in_memory();
        let plan = compare(&mut rpc, &pkg, DIR, false, &mut cache).unwrap();
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
        assert_eq!(plan.hashed, 2);

        let total = plan.uploads().map(PkgFile::len).sum();
        let hashed_before = card.borrow().md5_requests;
        let unchecked = upload(&mut rpc, &plan, &pkg, DIR, &args(&[]), total, &mut cache).unwrap();
        for f in &pkg.files {
            assert_eq!(card.borrow().files[&format!("{DIR}/{}", f.name)], f.data, "{}", f.name);
        }
        let manifest_len = pkg.files.last().unwrap().data.len();
        // 4096-byte messages
        assert_eq!(card.borrow().pieces, [4096, 4096, 808, manifest_len]);
        // the manifest is checked; firmware.dfu, whose CRC the updater checks, isn't
        assert_eq!(card.borrow().md5_requests - hashed_before, 1);
        assert_eq!(unchecked.len(), 1);
        assert_eq!(unchecked[0].0, format!("{DIR}/firmware.dfu"));

        rpc.delete_all(&[format!("{DIR}/res-old.ths")]).unwrap();
        assert!(!card.borrow().files.contains_key(&format!("{DIR}/res-old.ths")));

        // a second look finds nothing to send, and hashes only what the cache doesn't know yet
        let again = compare(&mut rpc, &pkg, DIR, false, &mut cache).unwrap();
        assert_eq!(again.uploads().count(), 0);
        assert!(again.extras.is_empty());
        assert_eq!(again.hashed, 1); // firmware.dfu: cached only once an install has gone through
        for (path, seen) in unchecked {
            cache.set(&path, seen);
        }
        let third = compare(&mut rpc, &pkg, DIR, false, &mut cache).unwrap();
        assert_eq!(third.hashed, 0);

        // someone rewrites a file with the same size: its timestamp moves, so it's hashed again
        card.borrow_mut().put(&format!("{DIR}/res-a.ths"), b"RESOURCES");
        let fourth = compare(&mut rpc, &pkg, DIR, false, &mut cache).unwrap();
        assert_eq!(fourth.hashed, 1);
        assert_eq!(fourth.uploads().map(|f| f.name.as_str()).collect::<Vec<_>>(), ["res-a.ths"]);

        assert!(!rpc.app_open().unwrap());
        assert_eq!(rpc.prepare_update(&format!("{DIR}/{MANIFEST}")).unwrap(), 0);
        assert_eq!(rpc.prepare_update("/ext/update/nope/update.fuf").unwrap(), 2);
        rpc.reboot_to_update().unwrap();
        assert!(card.borrow().rebooted);
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn verify_checks_everything() {
        let root = scratch("verify");
        let pkg = make_package(&root);
        let card = Rc::new(RefCell::new(Card::default()));
        let mut rpc = session(&card);
        let mut cache = Cache::in_memory();
        let plan = compare(&mut rpc, &pkg, DIR, false, &mut cache).unwrap();
        let unchecked =
            upload(&mut rpc, &plan, &pkg, DIR, &args(&["--verify"]), pkg.total(), &mut cache)
                .unwrap();
        assert!(unchecked.is_empty());
        assert_eq!(card.borrow().md5_requests, 3);
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
        let mut cache = Cache::in_memory();
        let plan = compare(&mut rpc, &pkg, DIR, false, &mut cache).unwrap();
        assert!(!plan.folder_exists);
        assert!(plan.steps.iter().all(|s| s.action == Action::New));
        // batched: messages handed to the port 16 KB at a time arrive the same
        upload(&mut rpc, &plan, &pkg, DIR, &args(&["--batch", "16384"]), pkg.total(), &mut cache)
            .unwrap();
        assert_eq!(card.borrow().pieces, [4096, 4096, 808, 9, pkg.files[2].data.len()]);
        assert!(card.borrow().dirs.contains(DIR));
        assert_eq!(card.borrow().files.len(), 3);
        assert_eq!(rpc.stat_size(&format!("{DIR}/res-a.ths")).unwrap(), Some(9));
        assert_eq!(rpc.stat_size(updatelog::LOG_PATH).unwrap(), None);

        // the update log, read from where it was
        card.borrow_mut().put(updatelog::LOG_PATH, b"[1] old\n[2] stage 17, progress 100\n");
        let report = read_new_log(&mut rpc, 8).unwrap();
        assert_eq!(report.outcome, Outcome::Done);
        assert_eq!(report.lines.len(), 1);
        rpc.stop();
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn force_sends_files_that_match() {
        let root = scratch("force");
        let pkg = make_package(&root);
        let card = Rc::new(RefCell::new(Card::default()));
        card.borrow_mut().dirs.insert(DIR.into());
        card.borrow_mut().put(&format!("{DIR}/res-a.ths"), b"resources");
        let mut rpc = session(&card);
        let plan = compare(&mut rpc, &pkg, DIR, true, &mut Cache::in_memory()).unwrap();
        assert_eq!(plan.steps[1].action, Action::Forced);
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn many_requests_in_a_window() {
        // more requests than fit in the Flipper's 1 KB buffer at once
        let card = Rc::new(RefCell::new(Card::default()));
        let paths: Vec<String> = (0..60).map(|i| format!("{DIR}/res-{i:02}-abcdef.ths")).collect();
        for p in &paths {
            card.borrow_mut().put(p, p.as_bytes());
        }
        let mut rpc = session(&card);
        let stamps = rpc.timestamps(&paths).unwrap();
        assert!(stamps.iter().all(Option::is_some));
        let sums = rpc.md5s(&paths).unwrap();
        assert_eq!(sums[59], format!("{:x}", <md5::Md5 as md5::Digest>::digest(paths[59].as_bytes())));
        assert!(card.borrow().most_unanswered <= 768, "{}", card.borrow().most_unanswered);
    }
}
