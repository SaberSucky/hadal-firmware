//! Hadal's update log (/ext/update/hadal_update.log): what the updater did, read after it's done

/// applications/system/updater/util/update_log.h
pub const LOG_PATH: &str = "/ext/update/hadal_update.log";

/// UpdateTaskStage (applications/system/updater/util/update_task.h)
const STAGE_COMPLETED: u32 = 17;
const STAGE_ERROR: u32 = 18;

fn stage_name(stage: u32) -> &'static str {
    match stage {
        1 => "reading the manifest",
        2 => "backing up internal storage",
        3 => "checking the radio stack",
        4 => "removing the old radio stack",
        5 => "writing the radio stack",
        6 => "installing the radio stack",
        7 => "waiting for the radio core",
        8 => "checking the option bytes",
        9 => "checking the firmware image",
        10 => "writing the firmware",
        11 => "verifying the firmware",
        12 => "restoring internal storage",
        13 => "removing old files",
        14 => "removing old folders",
        15 => "unpacking resources",
        16 => "installing the splash screen",
        17 => "done",
        18 => "error",
        19 => "option bytes error",
        _ => "an unknown stage",
    }
}

#[derive(Debug, PartialEq)]
pub enum Outcome {
    Done,
    Failed(String),
    /// The log says nothing either way
    Unknown,
}

pub struct Report {
    pub outcome: Outcome,
    /// The new lines, without their tick prefix
    pub lines: Vec<String>,
}

/// What was added since `offset`; all of it if the log was started over in between
pub fn since(log: &[u8], offset: u64) -> &[u8] {
    match usize::try_from(offset) {
        Ok(o) if o <= log.len() => &log[o..],
        _ => log,
    }
}

pub fn parse(new_part: &[u8]) -> Report {
    let text = String::from_utf8_lossy(new_part);
    let mut outcome = Outcome::Unknown;
    let mut last_stage: Option<u32> = None;
    // The boot guard put the previous firmware back: whatever else the log says after that
    let mut rolled_back: Option<String> = None;
    let mut lines = Vec::new();
    for raw in text.lines() {
        // "[    1234] text": the tick since that start
        let line = match raw.trim_start().strip_prefix('[') {
            Some(rest) => rest.split_once("] ").map_or(raw, |(_, t)| t),
            None => raw,
        }
        .trim_end();
        if line.is_empty() {
            continue;
        }
        lines.push(line.to_string());

        if let Some(rest) = line.strip_prefix("stage ") {
            let Some(stage) = rest.split(',').next().and_then(|s| s.trim().parse::<u32>().ok())
            else {
                continue;
            };
            if stage == STAGE_COMPLETED {
                outcome = Outcome::Done;
            } else if stage >= STAGE_ERROR {
                outcome = Outcome::Failed(match last_stage {
                    Some(s) => format!("{} while {}", stage_name(stage), stage_name(s)),
                    None => stage_name(stage).to_string(),
                });
            } else {
                last_stage = Some(stage);
            }
        } else if let Some(reason) = line.strip_prefix("refused: ") {
            outcome = Outcome::Failed(reason.to_string());
        } else if line.contains("last step") && line.contains("(failed)") {
            outcome = Outcome::Failed(line.to_string());
        } else if line.starts_with("rollback: ") && line.contains("put back") {
            rolled_back = Some(format!(
                "the new firmware didn't start, so the Flipper put the previous one back ({line})"
            ));
        }
    }
    if let Some(why) = rolled_back {
        outcome = Outcome::Failed(why);
    }
    Report { outcome, lines }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn completed() {
        let log = b"[  100] --- updater started, boot mode 3\n[  120] stage 13, progress 0\n[  900] stage 15, progress 0\n[ 4000] stage 17, progress 100\n";
        let r = parse(log);
        assert_eq!(r.outcome, Outcome::Done);
        assert_eq!(r.lines[0], "--- updater started, boot mode 3");
    }

    #[test]
    fn failed_names_the_stage() {
        let r = parse(b"[1] stage 10, progress 0\n[2] stage 18, progress 0\n");
        assert_eq!(r.outcome, Outcome::Failed("error while writing the firmware".into()));
        let r = parse(b"[1] refused: not enough free space\n");
        assert_eq!(r.outcome, Outcome::Failed("not enough free space".into()));
    }

    #[test]
    fn the_last_word_counts() {
        let r = parse(b"[1] stage 18, progress 0\n[2] --- updater started, boot mode 2\n[3] stage 17, progress 100\n");
        assert_eq!(r.outcome, Outcome::Done);
    }

    #[test]
    fn a_rollback_is_a_failure() {
        let r = parse(b"[1] rollback: hadal-1 put back, the new firmware did not start\n[2] stage 17, progress 100\n");
        assert!(matches!(r.outcome, Outcome::Failed(_)));
    }

    #[test]
    fn since_offset() {
        assert_eq!(since(b"old\nnew\n", 4), b"new\n");
        assert_eq!(since(b"new\n", 100), b"new\n"); // started over
    }
}
