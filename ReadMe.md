# Hadal Firmware

**Custom firmware for the Flipper Zero that goes deeper.**

Hadal is built on top of [Unleashed Firmware](https://github.com/DarkFlippers/unleashed-firmware), which itself is based on the [official Flipper Zero firmware](https://github.com/flipperdevices/flipperzero-firmware). It keeps everything Unleashed offers and adds features the Flipper normally can't do on its own, starting with Bluetooth LE scanning using only the Flipper's built-in radio. No ESP32 or other add-on board needed.

> [!WARNING]
> Hadal is in **early development**. New features may not yet be verified on real hardware. Use it at your own risk.
>
> This software is intended for experimental and educational purposes only. Use it only within the bounds of the law, and only on devices and signals you own or have permission to work with.
>
> Hadal is an independent project. It is **not affiliated with Flipper Devices or the Unleashed team**. Please don't report Hadal issues to them: open an issue in this repository instead.

<br/>

## 🌊 What's different from Unleashed

### Bluetooth LE scanning
The Flipper ships with ST's "BLE Light" radio stack, which can only advertise and accept connections. It can't hear other devices. Hadal switches the radio to ST's **"BLE Full"** stack, which adds the observer role, and adds a scanning API on top of it.

- **BLE Scanner** app (Apps → Bluetooth): lists nearby Bluetooth LE devices by signal strength, and recognises Find My accessories (AirTags and similar), AirPods, Samsung SmartTags, Tile trackers and Google Find My Device tags
- **Tracker Detector** (Apps → Bluetooth): keep it open and it warns you (vibration, red light, a full-screen message) when a tracker that is away from its owner has moved along with you for 10 minutes: AirTags and other Find My accessories, Google Find My Device tags, SmartTags and Tiles. AirTags and Google tags say when their owner is not near, so one whose owner walks beside you is listed but not warned about. OK on a tracker shows how near it is, to find it
- **Find My Stuff**: every device around, strongest first, by name where it says one. Pick yours (earbuds, a watch, your own tag) and a hotter / colder meter walks you to it, clicking faster as you get closer
- **Crowd Meter**: how busy a place is, from the devices heard in the last minute, with a graph of the last 20 minutes and what kinds of device they are. It counts devices, not people
- Passive scanning (silent, only listens) or active scanning (also asks devices for their names)
- Normal Bluetooth (phone app, Bluetooth remotes) keeps working, and scanning survives Bluetooth profile switches
- `furi_hal_bt_scanner_*` API for apps

### Hadal settings
**Settings → Hadal** gathers what Hadal adds, in pages:

- **Interface → Main Menu:** put the main menu in your own order, take apps out, and add any app from the SD card. Kept in `/ext/apps_data/loader/main_menu.txt` (one entry per line); without it the menu has the built-in order, and Settings is always in it
- **Interface → Menu Style:** the main menu's look (the built-in list or one of the style plugins), applied at once
- **Interface → Asset Pack:** see below
- **Interface → Desktop Keys:** what each button does on the desktop, pressed or held, all ten of them (the ones that were fixed too): a desktop action (main menu, lock menu, lock, archive, power off, nothing), any app, or any file, which opens in its app (a `.sub` in Sub-GHz, a `.ir` in Infrared...). Kept in `/ext/apps_data/desktop/keybinds.txt`, one line a button; a button with no line does what it always did
- **Interface → File Browser:** how Archive and the file pickers apps open sort files (by name, name Z-A, newest, largest or type, folders first or mixed in), and whether Archive's Browser tab shows hidden files. Archive also gets **Search** in the Browser tab's menu: type part of a name and it finds every file and folder under the folder you are in (from the top of the SD card when you are at the top), and picking one opens its folder on it
- **Interface → Lock Screen:** lock at start or not when a PIN is set, the time and date on the lock screen, and what too many wrong PINs in a row do: nothing, power off, or erase the Flipper (its settings, PIN, pairings and saved keys; the SD card is kept). The PIN screen says how many tries are left
- **Protocols → Extended Sub-GHz:** the range beyond the radio chip's specification, which used to need a file edited by hand. It asks first, and applies after a restart

### Keyboard
The on-screen keyboard has a symbols page: the **#** key beside **_** switches between letters and every printable ASCII symbol (`! @ # $ % ^ & * ( ) ~ \` | \ - + = [ ] { } ; : < > " ' , . / ?`), with a space bar, for passwords, URLs and file names. As before, **Up** from the top row moves into the text, where **Left** and **Right** move the cursor.

### Updates and crashes
- **The update screen:** a bathyscaphe sinks towards the bottom of the Challenger Deep (10,935 m) as the update goes on, with a depth gauge, the ocean zone it has reached, marine snow rising past it, and the seabed coming up at the end
- **Only what changed:** an update writes only the SD files that differ (each file's real size and MD5 are checked against the new package), instead of all ~1,700
- **Checked first:** before changing anything, the updater checks the battery, both images, that the firmware fits beside the radio stack, and that the option bytes cannot lock the chip. If something is wrong it stops with the reason, and the Flipper keeps its firmware
- **Rollback:** a firmware that fails to start four times in a row is replaced by the last one that ran steadily (kept in `/ext/update/.hadal_rollback`), with your settings and files left as they are
- **Crash reports:** after a crash, `/ext/crash/` gets a report: the thread, the message, where it happened and the call chain. `python3 scripts/crash_report.py <report>` names the functions
- `update/hadal_update.log` on the SD card records every update
- **Start-up timeline:** 30 seconds after each start, `/ext/apps_data/boot/timeline.txt` lists when each step of the start ended and how long it took (the hardware, the SD card, the radio, every start hook, the desktop). The `boot_profile` CLI command prints the same

### Asset packs
**Settings → Hadal → Interface → Asset Pack** (also Settings → Desktop) switches the animations, icons and fonts to those of a pack on the SD card (`/ext/asset_packs/<Name>/`), without reflashing. The folder layout is compatible with the one other custom firmwares use, and everything that loads it is written for Hadal. A pack that is missing or broken changes nothing, and one that crashes the firmware is turned off at the next start. See [documentation/AssetPacks.md](documentation/AssetPacks.md).

- **Hadal** is the pack that ships with the firmware: deep-sea idle animations (an anglerfish lit by its own lure, that talks; a jellyfish; a bathyscaphe; a black smoker with tube worms and a crab), drawn as dark shapes in pale water, which is what the Flipper's screen shows best. Also status bar and main menu icons, a lighter interface font and instrument-panel clock numerals. It is generated by script (`assets/hadal_pack/build.py`)
- The first-start and "firmware updated" slideshows are Hadal's own, and credit Unleashed

### Universal remotes
- **Most-used codes first:** each button sends the codes that the most device models use first, counted in the public [Flipper IR database](https://github.com/Lucaslhm/Flipper-IRDB) (`scripts/rank_ir_library.py` writes the order next to each library; the libraries themselves are unchanged)
- **No repeats:** a code the library holds more than once (twice for one model, as NEC and as NECext, or once recorded raw) is sent once, the clean version when there is one. Projector Power goes from 138 codes to 67, LED Power on from 70 to 48
- **Speed** (Infrared → Settings → Universal Speed): *Normal* keeps the original timings; *Fast* (default) leaves the gap a real remote leaves between repeats; *Turbo* leaves only the silence an IR receiver needs after the code's longest burst, and sends raw codes once (Sony-style codes three times, as Sony devices want). A full TV Power run takes about 45 s in Fast and 27 s in Turbo
- **Learns your devices:** a code you save from a universal remote run, or send again by hand from the pause screen, goes first the next time you press that button (kept in `/ext/apps_data/infrared/learned.txt`). Your TV's Power code ends up being the first one sent
- **Remote Finder:** when a code works, pause and *save all* to keep that device's whole remote
- The pause screen fits all its options

### Battery history
The Flipper writes its charge to the SD card every 10 minutes and whenever the charger is plugged in or out (8 bytes each, about two weeks a file, the two newest files kept in `/ext/apps_data/battery/`). **Battery History** (Apps → Tools) draws it over a day, three days, a week or two weeks, marks when it was charging and when it was off, and says how long the charge will last at the rate it has been going down since you last unplugged it.

### Making room for it
The Full radio stack needs 36 KB more of the Flipper's internal flash. To make it fit, some parts of the firmware were moved onto the SD card (877,964 → 796,776 bytes):

| Change | Effect |
|---|---|
| Sub-GHz runs as an SD card app | Opens a moment slower. Boot-time Sub-GHz settings still apply from the firmware |
| Power and System settings run as SD card apps | No visible change |
| JS engine moved into the JS app | JS scripts have ~43 KB less free RAM while running |

Storage settings intentionally stays built in, so you can still format or repair an SD card from the Flipper.

Everything else (Sub-GHz protocols, NFC, RFID, Infrared, BadUSB and the rest) comes from Unleashed. See the [Unleashed README](https://github.com/DarkFlippers/unleashed-firmware#readme) for the full feature list.


## 🚀 Installing

There are no prebuilt releases yet. Build the update package yourself (see below).

> [!WARNING]
> **A default build installs the BLE "Full" radio stack.** Switching a Flipper from the Light stack to the Full one used to leave it with a dark screen until it was repaired through qFlipper. The likely cause: the Full stack sits lower in flash (`0x080CE000` instead of `0x080D7000`), the updater wrote it before the new firmware, and a firmware built for the Light stack (Unleashed's is about 878 KB) still had its last ~34 KB, its startup data, where the stack went. The Flipper restarts several times while the radio is changed, each time from the old firmware, which could no longer start. The updater now writes the new firmware first when it fits below the new stack (Hadal's ends at about 805 KB), so every restart runs a firmware the stack does not touch. This fix has not yet been seen on a real device: keep qFlipper at hand, read [documentation/KeyCombo.md](documentation/KeyCombo.md) for the DFU button combinations, and if the install stalls, repair through qFlipper. After any update, `update/hadal_update.log` on the SD card says how far the radio part got.
>
> For a package whose radio and option bytes are identical to those of every other firmware, build with the standard radio stack:
>
> ```
> ./fbt COMPACT=1 DEBUG=0 updater_package COPRO_OB_DATA=scripts/ob.data COPRO_STACK_BIN=stm32wb5x_BLE_Stack_light_fw.bin COPRO_STACK_TYPE=ble_light
> ```
>
> The BLE Scanner then reports that scanning is unsupported; everything else works.

Check a package before you flash it. This re-derives from the files what the updater will do, and compares it with the standard configuration:

```
python3 scripts/update_package_check.py dist/f7-C/flipper-z-f7-update-local.tgz --expect full    # a default build
python3 scripts/update_package_check.py dist/f7-C/flipper-z-f7-update-local.tgz --expect light   # a standard-radio build
```

Then:

1. Open [qFlipper](https://flipperzero.one/update) and connect your Flipper
2. Choose **Install from file** and pick `dist/f7-C/flipper-z-f7-update-local.tgz`

With the standard radio stack the Flipper's radio is left alone. A Full-stack build replaces it (Light → Full): the Flipper restarts several times, this takes a few minutes, and you may need to pair your phone again.

**Going back:** installing official or Unleashed firmware through qFlipper restores the Light radio stack. If the Flipper ever fails to boot, use qFlipper's repair mode (see `documentation/KeyCombo.md` for the DFU button combinations).


## 👨‍💻 Building

Hadal builds exactly like Unleashed and the official firmware. See [How to build](/documentation/HowToBuild.md) for setup.

```bash
./fbt COMPACT=1 DEBUG=0 updater_package
```

Use release builds (`COMPACT=1 DEBUG=0`). Debug builds are larger and may not fit next to the BLE Full radio stack.

### Bundled apps
Hadal can bundle a large set of community apps (games, tools, GPIO, NFC, Sub-GHz and more), so a fresh install is not bare. They are other people's apps, fetched from a public collection at a pinned commit and compiled against Hadal's own SDK, never modified. Fetch them once before building, and they go into the update package like any app:

```bash
python3 scripts/bundle_apps.py fetch      # stage the apps listed in applications/bundled_apps.json
./fbt COMPACT=1 DEBUG=0 updater_package   # the package now carries them (about 13 MB)
```

The list, its source and each app's author and licence file are in [THIRD_PARTY_APPS.md](THIRD_PARTY_APPS.md). Some of the apps ship no licence file; check that is acceptable to you before you redistribute a build.

- **Developer documentation:** [developer.flipper.net](https://developer.flipper.net/flipperzero/doxygen)
- **Writing apps:** [AppsOnSDCard](/documentation/AppsOnSDCard.md), [AppManifests](/documentation/AppManifests.md)
- **Screen previews:** `python3 scripts/screen_preview.py` draws app screens on a PC with the firmware's own drawing code and fonts, into `build/screen_preview/` (after one firmware build)

### Project structure

- `applications`  - Applications and services used in firmware
- `assets`        - Assets used by applications and services
- `furi`          - Furi Core: OS-level primitives and helpers
- `lib`           - Libraries, drivers and third-party code
- `targets`       - Hardware targets, including the BLE radio glue (`targets/f7/ble_glue`)
- `site_scons`    - Build helpers
- `scripts`       - Supplementary scripts and Python libraries
- `documentation` - Documentation and guides


## 📁 Useful resources

- [Official Flipper documentation](https://docs.flipper.net)
- [UberGuidoZ Playground](https://github.com/UberGuidoZ/Flipper): large collection of IR, Sub-GHz and other files
- [Awesome Flipper Zero](https://github.com/djsime1/awesome-flipperzero)
- [Flipper IRDB](https://github.com/Lucaslhm/Flipper-IRDB): infrared remotes database


## ❤️ Credits

Hadal would not exist without the work it is built on:

- **[Unleashed Firmware](https://github.com/DarkFlippers/unleashed-firmware)** by @xMasterX and the Unleashed team and contributors. Nearly everything in Hadal beyond the official firmware is their work. If you enjoy it, please support the Unleashed developers (see "Please support development of the project" in [their README](https://github.com/DarkFlippers/unleashed-firmware#readme)).
- **[Official Flipper Zero firmware](https://github.com/flipperdevices/flipperzero-firmware)** by Flipper Devices
- **STMicroelectronics** for the STM32WB wireless coprocessor stacks
- **[Flipper IRDB](https://github.com/Lucaslhm/Flipper-IRDB)** (CC0) by Lucaslhm and its contributors, whose remotes decide the order universal remotes send codes in
- Everyone who contributed to the projects above


## 📜 License

Hadal Firmware is free software, licensed under the **GNU General Public License v3.0**, the same license as Unleashed and the official firmware. See [LICENSE](/LICENSE).

As a derivative work of Unleashed Firmware and the official Flipper Zero firmware, Hadal keeps all of their copyright notices. Hadal's own changes are released under the same license, and the complete source code is available in this repository.

Third-party components (such as the STM32WB coprocessor binaries) are covered by their own licenses, included alongside them.
