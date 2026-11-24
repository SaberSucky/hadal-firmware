# hadal-update

Installs a Hadal update package on a Flipper Zero over USB, and sends only the files that changed.

qFlipper uploads the whole package (3.6 MB, about 36 s) every time. Between two builds, usually only
`firmware.dfu`, `update.fuf` and one or two `res-*.ths` archives change. The upload speed itself
can't go much higher: the Windows USB-serial driver and the SD card cap it. So this tool compares
each file with the copy already in `/ext/update/<package>/` on the Flipper and sends only the ones
that differ.

On a Flipper, with 4 of 24 files changed: 990 KB sent in 6.9 s, the Flipper off USB for 11.2 s
while it updates, back and checked 0.7 s later, 23 s in all. That run's comparison took 3.5 s
because another tool had rewritten the files; once this tool has seen them it takes 0.1 s, and
the whole install about 20 s.

## Build

```powershell
cd tools\hadal-update
cargo build --release
```

This gives `target\release\hadal-update.exe`, a single file with no runtime dependencies. It needs
only the stable Rust toolchain (it's built with `stable-x86_64-pc-windows-gnu`), no protoc.

## Use

```powershell
hadal-update --dry-run                          # what would be sent and removed; writes nothing
hadal-update                                    # send what changed, install, wait for the result
hadal-update dist\f7-C\flipper-z-f7-update-local.tgz
```

With no package given, it takes the newest `dist/<target>/<package>/` in the nearest folder above the
current one that has a `dist/`, so running it anywhere in the repo installs the latest build. A
package can be its folder, its `update.fuf`, or the `.tgz` beside it.

| Option | |
|---|---|
| `-n`, `--dry-run` | Compare, list what would be sent and removed, and stop. Reads from the Flipper and writes nothing. |
| `-p`, `--port COM3` | Use this port. Default: the Flipper found by its USB id (0483:5740). |
| `-f`, `--force` | Send every file, even those already there. |
| `--verify` | Check every sent file's MD5 on the Flipper, also those the updater checks by CRC itself. |
| `--no-install` | Send, then stop without installing. |
| `--no-wait` | Start the install and exit without waiting for the Flipper to come back. |
| `--keep-extra` | Keep old package files that this package doesn't have (they're removed otherwise). |
| `--dir-name NAME` | The folder under `/ext/update/` (default: the package's name, as qFlipper and `selfupdate.py` use). |
| `--chunk BYTES` | File data per write, 512 to 16384 (default 4096). |
| `-v`, `--verbose` | Print every RPC message. |

Close qFlipper first: only one program can have the port open.

## How it works

It speaks the Flipper's RPC (protobuf on the same serial port, after `start_rpc_session`), following
the flow of `scripts/selfupdate.py` and `scripts/flipper/storage.py`:

1. Lists `/ext/update/<package>/` in one call. A file that's missing or has a different size is
   sent. For a file of the same size, the MD5 decides. The tool keeps the MD5s it has seen, with each
   file's size and timestamp on the card, in `%LOCALAPPDATA%\hadal-update\<Flipper's UID>.txt`;
   a file whose size and timestamp are unchanged isn't hashed again. The Flipper hashes the others
   at about 1 MB/s.
2. Sends the changed files in 4 KB pieces, `update.fuf` last, so the manifest on the card never names
   a file that hasn't arrived yet. After each file it checks the MD5 on the Flipper and sends the file
   again if it differs, except for the files the updater checks by CRC before it uses them
   (`firmware.dfu`, `updater.bin`, `radio.bin`): checking the firmware's MD5 alone took 0.7 s.
3. Removes old package files in that folder (archives, images, manifests) that this package doesn't
   have, such as a `res-*.ths` for a group that's no longer built. It leaves alone what the updater
   keeps there: `.installed` (what the last install unpacked, so the next one skips unchanged
   archives), `backup.tar` (the internal storage backup) and any other file.
4. Waits while an app is open, as the firmware's own `update install` does. Then it has the Flipper
   check the package and restarts it into the updater.
5. Follows the update from outside: it watches the USB port every 100 ms, and while the updater app
   is running (`loader info`) it keeps the port open until the Flipper restarts. Once the Flipper is
   back, it reads only what this update added to `/ext/update/hadal_update.log`, then reports the
   result, the firmware the Flipper now runs, and when the Flipper went off USB and came back.

Small requests (timestamps, hashes, deletes) go without waiting for each answer, with at most
768 bytes unanswered: the Flipper's RPC reads through a 1 KB buffer, and its command line drops
bytes it can't place there within 3 s.

## Where the time goes

Of a 23 s install, about 17 s is out of this tool's hands:

- **Sending `firmware.dfu`: 5.8 s.** It changes with every build, and writes top out at about
  148 KB/s on the Flipper's side: 4 KB or 8 KB messages, one per USB write or batched in 32 KB,
  all measured the same.
- **The Flipper's own update: 11.2 s** off USB: three restarts, the flash write, the resources.

What's left is the tool's: about 0.4 s to connect, 0.1 s to compare (with its cache), 0.4 s for
the Flipper to check the package, 0.7 s to see it back. Going much faster would take firmware
changes, in the RPC's USB path or in the updater.

Serial writes get their own long timeout. The serial crate has one timeout for reads and writes,
and a 32 KB write cut short by a 100 ms read timeout left the Flipper's RPC stream garbled.

It never sends key presses. If the Flipper is PIN-locked, its command line is off: unlock it on the
Flipper.

`cargo test` runs the tests against an in-memory fake Flipper; they need no device.
