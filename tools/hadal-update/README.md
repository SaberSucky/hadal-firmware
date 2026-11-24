# hadal-update

Installs a Hadal update package on a Flipper Zero over USB, and sends only the files that changed.

qFlipper uploads the whole package (3.6 MB, about 36 s) every time. Between two builds, usually only
`firmware.dfu`, `update.fuf` and one or two `res-*.ths` archives change. The upload speed itself
can't go much higher: the Windows USB-serial driver and the SD card cap it. So this tool compares
each file with the copy already in `/ext/update/<package>/` on the Flipper and sends only the ones
that differ.

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
| `--no-install` | Send, then stop without installing. |
| `--no-wait` | Start the install and exit without waiting for the Flipper to come back. |
| `--keep-extra` | Keep files in the Flipper's package folder that this package doesn't have (they're removed otherwise). |
| `--dir-name NAME` | The folder under `/ext/update/` (default: the package's name, as qFlipper and `selfupdate.py` use). |
| `--chunk BYTES` | File data per write, 512 to 16384 (default 4096). |
| `-v`, `--verbose` | Print every RPC message. |

Close qFlipper first: only one program can have the port open.

## How it works

It speaks the Flipper's RPC (protobuf on the same serial port, after `start_rpc_session`), following
the flow of `scripts/selfupdate.py` and `scripts/flipper/storage.py`:

1. Lists `/ext/update/<package>/` in one call. A file that's missing or has a different size is
   sent; for a file of the same size, it asks the Flipper for its MD5.
2. Sends the changed files in 4 KB pieces, `update.fuf` last, so the manifest on the card never names
   a file that hasn't arrived yet. After each file it checks the MD5 on the Flipper, and sends the file
   again if it differs.
3. Removes files in that folder that this package doesn't have.
4. Waits while an app is open, as the firmware's own `update install` does. Then it has the Flipper
   check the package and restarts it into the updater.
5. Follows the update from outside: it watches the USB port and asks `loader info` whether the
   updater app is still running. Once the Flipper is back, it reads what this update added to
   `/ext/update/hadal_update.log`, then reports the result and the firmware the Flipper now runs.

It never sends key presses. If the Flipper is PIN-locked, its command line is off: unlock it on the
Flipper.

`cargo test` runs the tests against an in-memory fake Flipper; they need no device.
