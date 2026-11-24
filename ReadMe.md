<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/logo/hadal-logo-dark.png">
    <source media="(prefers-color-scheme: light)" srcset="assets/logo/hadal-logo.png">
    <img alt="Hadal Firmware: Abyss the anglerfish beside the name" src="assets/logo/hadal-logo.png">
  </picture>
</p>

<p align="center">
  <b>Custom firmware for the Flipper Zero that goes deeper.</b><br>
  Built on <a href="https://github.com/DarkFlippers/unleashed-firmware">Unleashed</a>, with Abyss the anglerfish lighting the way.
</p>

> [!WARNING]
> Hadal is in early development and some features have not been tried on real hardware. Use it at your own risk, within the law, and only on devices and signals you own or may work with. Hadal is independent of Flipper Devices and the Unleashed team: please report its issues here, not to them.

## Highlights

- **Bluetooth LE scanning, no add-on board.** Hadal runs ST's *Full* radio stack, so the Flipper can listen as well as advertise. Apps → Bluetooth gets **BLE Scanner**, **Tracker Detector** (warns when an AirTag, SmartTag, Tile or Find My Device tag travels with you), **Find My Stuff** (a hotter/colder meter to your own earbuds or tag) and **Crowd Meter**.
- **A look of its own.** Settings → Hadal → Interface sets the **Header Style** (Fade, Rule, Dots or Plain) and the **Menu Style**: the built-in list or one of eleven others, among them **Sonar** and **PS4**. Up on the desktop opens a **control centre** with dark mode, brightness and volume.
- **Asset packs, previewed live.** **Asset Packs** plays each pack's own animation full screen before you choose it. Hadal's own deep-sea pack comes with it, and packs made for Momentum load whole.
- **An alarm that does more.** The Clock app's alarm repeats once, every day, on weekdays, on weekends or on days you pick. It snoozes and rings for 1–30 min, with a gentle start, vibration and light.
- **Faster, and easier on the battery.** Apps open up to 37% sooner (Infrared in 107 ms, was 170). An update sends about 1 MB instead of 2.7 MB and unpacks only what changed. Settings, Archive, the Clock and the Bluetooth tools let the chip sleep deeply between key presses.
- **Safe updates and crash reports.** The updater checks a package before it changes anything, and rolls back a firmware that fails to start four times. After a crash, `/ext/crash/` holds a report; `python3 scripts/crash_report.py <report>` names the functions.
- **Development mode.** With Settings → Hadal → Interface → Lock Screen → Development on, a crash, reboot or update while unlocked and on USB comes back unlocked, without the PIN.
- **And more:** Sub-GHz signals you can ignore, de-duplicate, autosave and tag with your phone's location; your own Sub-GHz and hopper frequencies; universal remotes that send the most-used codes first and learn yours; Battery History; levels up to 30; a symbols page on the keyboard; sorting and search in the file browser; every desktop button yours to set.

## Install

There are no prebuilt releases yet: build the update package (below), then:

1. Open [qFlipper](https://flipperzero.one/update) and connect the Flipper.
2. Choose **Install from file** and pick `dist/f7-C/flipper-z-f7-update-local.tgz`.

Coming from another firmware, the first install moves the Flipper's radio from the Light stack to the Full one. The Flipper restarts several times over a few minutes, and you may need to pair your phone again. If an install stalls, repair the Flipper through qFlipper (DFU key combinations: [KeyCombo.md](documentation/KeyCombo.md)); `update/hadal_update.log` on the SD card says how far it got. Installing official or Unleashed firmware through qFlipper takes you back.

## Build

Set up as for the official firmware ([How to build](documentation/HowToBuild.md)), then:

```
./fbt COMPACT=1 DEBUG=0 updater_package        # Windows: fbt.cmd COMPACT=1 DEBUG=0 updater_package
```

Use release builds: a debug build may not fit beside the Full radio stack. `python3 scripts/update_package_check.py <package> --expect full` checks a package before you flash it.

For a package with the standard radio stack (BLE Scanner then reports that scanning is unsupported; everything else works):

```
./fbt COMPACT=1 DEBUG=0 updater_package COPRO_OB_DATA=scripts/ob.data COPRO_STACK_BIN=stm32wb5x_BLE_Stack_light_fw.bin COPRO_STACK_TYPE=ble_light
```

To bundle community apps (fetched at a pinned commit, unmodified), run `python3 scripts/bundle_apps.py fetch` before building. Their authors and licences are in [THIRD_PARTY_APPS.md](THIRD_PARTY_APPS.md); some ship no licence file, so check before you redistribute a build.

## Credits

- **[Unleashed Firmware](https://github.com/DarkFlippers/unleashed-firmware)** by @xMasterX, the Unleashed team and contributors. Nearly everything in Hadal beyond the official firmware is their work; please [support them](https://github.com/DarkFlippers/unleashed-firmware#readme).
- **[Official Flipper Zero firmware](https://github.com/flipperdevices/flipperzero-firmware)** by Flipper Devices.
- **STMicroelectronics** for the STM32WB wireless coprocessor stacks.
- **[Flipper IRDB](https://github.com/Lucaslhm/Flipper-IRDB)** (CC0) by Lucaslhm and contributors, which sets the order universal remotes send codes in.

## Licence

GNU General Public License v3.0, as Unleashed and the official firmware are: see [LICENSE](LICENSE). Hadal keeps their copyright notices. Third-party parts, such as the STM32WB coprocessor binaries, come with their own licences.
