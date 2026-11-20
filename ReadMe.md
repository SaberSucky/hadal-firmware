<picture>
  <source media="(prefers-color-scheme: dark)" srcset="assets/logo/hadal-logo-dark.png">
  <source media="(prefers-color-scheme: light)" srcset="assets/logo/hadal-logo.png">
  <img alt="Hadal Firmware: Abyss the anglerfish beside the name" src="assets/logo/hadal-logo.png">
</picture>

# Hadal Firmware

**Custom firmware for the Flipper Zero that goes deeper.** Its mascot is Abyss, an anglerfish that lights its own way.

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
- **Tracker Detector** (Apps → Bluetooth): keep it open and it warns you (vibration, red light, a full-screen message) when a tracker that is away from its owner has moved along with you for 10 minutes: AirTags and other Find My accessories, AirPods on Find My (a case slipped into a bag tracks as well as a tag), Google Find My Device tags, SmartTags and Tiles. Your own, once marked as yours, never warn. AirTags and Google tags say when their owner is not near, so one whose owner walks beside you is listed but not warned about. OK on a tracker shows how near it is, to find it
- **Find My Stuff**: every device around, strongest first, by name where it says one. Pick yours (earbuds, a watch, your own tag) and a hotter / colder meter walks you to it, clicking faster as you get closer
- **Crowd Meter**: how busy a place is, from the devices heard in the last minute, with a graph of the last 20 minutes and what kinds of device they are. It estimates people from the phones and worn devices heard (not trackers, beacons or televisions), leaves out your own, and counts a device that changed its address once; Up and Down choose how far to count (near, the room, everything heard)
- Passive scanning (silent, only listens) or active scanning (also asks devices for their names)
- Normal Bluetooth (phone app, Bluetooth remotes) keeps working, and scanning survives Bluetooth profile switches
- `furi_hal_bt_scanner_*` API for apps

### Control centre
**Up** on the desktop opens the control centre, on the whole screen:

- **Tiles** for what you switch often: lock, lock with PIN, Bluetooth, silent, dark mode, left-handed, Hadal Settings, dummy mode, and two apps of your choice (Settings → Hadal → Interface → Desktop Keys → Up panel app 1 and 2). A tile that is on is filled; the line along the bottom says what the selected one is and does
- **Brightness and volume** as sliders: Up and Down move the selected one, OK on volume turns vibration on or off. They take effect as you move them and are saved when the panel closes
- **The time and the battery** (with the charge level and whether it is charging) along the top
- **Lock** locks the buttons only, even with a PIN set; **Lock with PIN** asks for it to unlock. With no PIN set, it opens the PIN settings
- **Dark mode** shows the whole screen inverted, light pixels on dark, in every app; it is kept across restarts

### Hadal settings
**Settings → Hadal** gathers what Hadal adds, in pages:

- **Interface → Main Menu:** put the main menu in your own order, take apps out, and add any app from the SD card. Kept in `/ext/apps_data/loader/main_menu.txt` (one entry per line); without it the menu has the built-in order, and Settings is always in it
- **Interface → Menu Style:** the main menu's look (the built-in list or one of the style plugins), applied at once
- **Interface → Asset Pack:** see below
- **Interface → Desktop Keys:** what each button does on the desktop, pressed or held, all ten of them (the ones that were fixed too): a desktop action (main menu, lock menu, lock, archive, power off, nothing), any app, or any file, which opens in its app (a `.sub` in Sub-GHz, a `.ir` in Infrared...). Kept in `/ext/apps_data/desktop/keybinds.txt`, one line a button; a button with no line does what it always did
- **Interface → File Browser:** how Archive and the file pickers apps open sort files (by name, name Z-A, newest, largest or type, folders first or mixed in), and whether Archive's Browser tab shows hidden files. Archive also gets **Search** in the Browser tab's menu: type part of a name and it finds every file and folder under the folder you are in (from the top of the SD card when you are at the top), and picking one opens its folder on it
- **Interface → Lock Screen:** lock at start or not when a PIN is set, the time and date on the lock screen, and what too many wrong PINs in a row do: nothing, power off, or erase the Flipper (its settings, PIN, pairings and saved keys; the SD card is kept). The PIN screen says how many tries are left
- **Misc → Spoofing:** the Bluetooth address (typed in, or random) and the serial (the unique id About and device info show), applied at the next start; the Flipper's own come back with one press. The name stays where Unleashed has it (Settings → Desktop)
- **Protocols → Extended Sub-GHz:** the range beyond the radio chip's specification, which used to need a file edited by hand. It asks first, and applies after a restart

### Sub-GHz
- **Ignore a signal:** on a received signal, **Ignore** puts it aside for good (a neighbour's doorbell, a weather station): it leaves the list and never shows again. Kept in `/ext/subghz/assets/ignored.txt`; **Read → Config → Forget ignored** brings them all back
- **Duplicates: Skip** (Read → Config): a signal already anywhere in the list is not added again, not only a repeat of the last one. A signal counts as the same whatever frequency, preset or timing it came in on; a rolling code is new each press
- **Autosave** (Read → Config): every new signal is saved to `/ext/subghz/autosave/` as it comes in, named by protocol, date and time
- **Location** (Read → Config → Location: Phone): every new signal is saved with where it was received (Latitude and Longitude lines in the `.sub`), from the Flipper app on your paired phone. No GPS module needed; with the phone away or its location off, signals are saved as before
- **Your own frequencies:** Settings → Hadal → Protocols → Sub-GHz Frequencies adds and removes frequencies in `setting_user`, and keeps or leaves out the built-in list; comments, hopper frequencies and custom presets in the file are left alone

### Levels
The dolphin goes up to **level 30**. The first three levels end where they always did (300 and 1,800 XP), so nobody's level changes; after that each level takes 1,500 XP and 100 more than the one before (level 30 starts at 77,401 XP). Animations made for "level 3" show at every level from 3 up, and the passport shows the level and how far it is to the next.

### Lists
- Menus with a title show it in Hadal's own bold capitals over a dotted rule, and the selected entry sits on a rounded bar (the same in settings lists)
- In every menu (the lists most apps and settings use), a selected entry too long for its row scrolls so all of it can be read, and **Left** and **Right** move a page at a time through a long list
- In the file pickers apps open, **Right** goes into the selected folder, as **Left** comes out of one
- Long text (Archive's Show, logs, info pages) pages with **Left** and **Right**
- Archive opens `.img` disk images in Mass Storage, and shows logs, CSV, JSON, config, INI, XML and YAML files as text
- After a crash, the notice says where the report is (`crash/` on the SD card)

### Keyboard
The on-screen keyboard has a symbols page: the **#** key beside **_** switches between letters and every printable ASCII symbol (`` ! @ # $ % ^ & * ( ) ~ ` | \ - + = [ ] { } ; : < > " ' , . / ? ``), with a space bar, for passwords, URLs and file names. As before, **Up** from the top row moves into the text, where **Left** and **Right** move the cursor.

### Updates and crashes
- **The update screen:** a bathyscaphe sinks towards the bottom of the Challenger Deep (10,935 m) as the update goes on, with a depth gauge, the ocean zone it has reached, marine snow rising past it, and the seabed coming up at the end
- **Only what changed:** an update writes only the SD files that differ (each file's real size and MD5 are checked against the new package), instead of all ~1,700
- **Faster uploads from qFlipper:** the files qFlipper sends in 1 KB pieces are written to the SD card 16 KB at a time, and checked and unpacked 4 KB at a time instead of 512 bytes. Every resource travels in an archive with the rest of its folder (the big ones alone, apps by app folder, app data by app, everything else by top folder), byte for byte the same from one build to the next, and qFlipper does not upload a file the card already holds with the same checksum: after the first update, an update sends the firmware, the updater, the Manifest (about 60 KB) and only the folders that changed, about 1 MB instead of 2.7 MB
- **Faster installs:** the updater keeps a note in the update folder (`.installed`) of the archives it unpacked. The next update from that folder skips each one whose checksum is unchanged without reading it, instead of unpacking all 14 MB to find the few files that changed, as long as nothing else has installed resources since (the card's `Manifest` must be the one it left). Deleting the update folder makes the next update check every file again
- **qFlipper sees the Flipper come back:** the USB port comes up once, early in the start and with its final name, so qFlipper is not left waiting for the Flipper after an update
- **Checked first:** before changing anything, the updater checks the battery, both images, that the firmware fits beside the radio stack, and that the option bytes cannot lock the chip. If something is wrong it stops with the reason, and the Flipper keeps its firmware
- **Rollback:** a firmware that fails to start four times in a row is replaced by the last one that ran steadily (kept in `/ext/update/.hadal_rollback`), with your settings and files left as they are
- **Crash reports:** after a crash, `/ext/crash/` gets a report: the thread, the message, where it happened and the call chain. `python3 scripts/crash_report.py <report>` names the functions
- `update/hadal_update.log` on the SD card records every update
- **Faster start:** each idle animation's frames are one file instead of one per frame (opening a file on the SD card means walking its folders again, which was most of the time an animation took to load), and the desktop brings up the USB port and the asset pack's folder before it loads the animation, once. Returning from an app reloads the animation the same quicker way
- **Start-up timeline:** 30 seconds after each start, `/ext/apps_data/boot/timeline.txt` lists when each step of the start ended and how long it took (the hardware, the SD card, the radio, every start hook, the desktop). The `boot_profile` CLI command prints the same

### Asset packs
**Settings → Hadal → Interface → Asset Pack** switches the animations, icons and fonts to those of a pack on the SD card (`/ext/asset_packs/<Name>/`), without reflashing. The folder layout is compatible with the one other custom firmwares use, and everything that loads it is written for Hadal. A pack that is missing or broken changes nothing, and one that crashes the firmware is turned off at the next start. See [documentation/AssetPacks.md](documentation/AssetPacks.md).

- **Hadal** is the pack that ships with the firmware: deep-sea idle animations (an anglerfish lit by its own lure, that talks; a jellyfish; a bathyscaphe; a black smoker with tube worms and a crab), drawn as dark shapes in pale water, which is what the Flipper's screen shows best, and a sonar ping for the loading spinner. It keeps the stock fonts and icons. It is generated by script (`assets/hadal_pack/build.py`)
- **The main menu's module icons** (Sub-GHz, NFC, Infrared and the rest) stay as they are whatever pack is in use
- **Packs made for Momentum** load whole: their fonts are taken when they are within a few pixels of the stock ones' size, and their icons when they are another size than the ones they replace. Packs another firmware installed (such as Momentum's Watch Dogs) are no longer deleted by Hadal's updater
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
