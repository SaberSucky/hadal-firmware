# Asset packs

An asset pack replaces Hadal's animations, icons and fonts with files from the SD card, without
reflashing. Pick one in **Settings → Desktop → Asset Pack**. With no pack selected nothing changes,
and anything a pack does not provide (or provides incorrectly) falls back to the built-in asset.

Packs live in `/ext/asset_packs/<Name>/`:

```
Anims/manifest.txt                      desktop animations, same layout as /ext/dolphin
Anims/<anim>/meta.txt
Anims/<anim>/frame_<N>.bm
Icons/<Category>/<Name>.bmx             static icon
Icons/<Category>/<Name>/meta            animated icon
Icons/<Category>/<Name>/frame_<NN>.bm
Fonts/<Slot>.u8f                        Primary, Secondary, Keyboard, BigNumbers, BatteryPercent
```

The layout is compatible with the one used by other custom firmwares, so existing packs mostly work.
An icon that Hadal does not have is ignored.

## What a pack can change

- **Animations**: the desktop idle animations. If the pack has `Anims/manifest.txt` they come from
  there instead of `/ext/dolphin`; the firmware's own system animations (SD card messages and so on)
  are always available. A new set starts at once if an animation is playing, otherwise when you
  next return to the desktop.
- **Icons**: only on screens the firmware itself draws, which is the status bar, the desktop, the
  main menu, the archive, the on-screen keyboard, the PIN screens and power screens. External apps
  carry their own copy of the icons they use, so a pack cannot restyle them. Hadal can only replace
  the 99 icons it actually keeps in flash; the rest of the 264 icon names are accepted by the packer
  but have no effect.
- **Fonts**: every screen of every app, because text is drawn by the firmware.

## Matching icons

An icon is matched by its path relative to `Icons/`, without the extension, for example
`StatusBar/Battery_26x8`. Names end in the icon size, and a replacement **must have the same
width, height and frame count**, otherwise it is skipped. Icons that are drawn with a mask
(`Foo_9x10` and `Foo_mask_9x10`) should be replaced together.

## File formats

- **Frame** (`.bm`): `0x00` followed by the raw bitmap, or `0x01 0x00 <u16 LE n>` followed by `n` bytes of
  heatshrink data (window 8, lookahead 4). The bitmap is 1 bit per pixel, rows padded to whole
  bytes, least significant bit first, a set bit is a lit pixel.
- **`.bmx`**: `<u32 LE width> <u32 LE height>` followed by one frame.
- **Animated icon `meta`**: `<u32 LE width> <u32 LE height> <u32 LE frame_rate> <u32 LE frame_count>`.
- **Font** (`.u8f`): the bytes of a u8g2 font, ending in a zero byte. The text renderer trusts a
  font completely, so the firmware checks it before use and ignores any font that fails:
  - maximum character height, ascent and descent must equal those of the font it replaces, because
    a different size would break the layout of every screen;
  - the glyph list must stay inside the file and end properly, and every glyph must hold all the
    data it asks for;
  - only characters 0 to 255 are supported (no Unicode section).

## Limits

Icons from a pack are held in RAM: at most 24 KB in total, and at most 128×128 pixels and 32
frames each. Fonts are at most 6 KB each and 12 KB together. Whatever does not fit is skipped, and
the log says why.

## While an app runs

The pack's icons are freed when an app starts, so that the app has the memory, and loaded again
when you return to the desktop; for that moment, and while the app runs, firmware-drawn parts such
as the status bar show the built-in icons. Fonts and animations stay as they are.

## If something goes wrong

- A pack folder that is missing, empty or full of invalid files simply changes nothing.
- A pack stays "on probation" until the firmware has run with it for ten seconds. If the Flipper
  resets before that, the next start turns the pack off (Settings → Desktop → Asset Pack goes back
  to None) instead of crashing again. This also means that a deliberate reset in the ten seconds
  after choosing a pack will switch it off; just choose it again.
- Delete the pack's folder from the SD card to remove it.

## Building a pack

`scripts/asset_packer.py` builds a pack from PNG sources laid out like `assets/icons`:

```
python3 scripts/asset_packer.py pack  <source> <output>   # source has Anims/, Icons/ and/or Fonts/
python3 scripts/asset_packer.py validate <pack>           # checks it the way the firmware will
python3 scripts/asset_packer.py preview  <pack> sheet.png # renders the icons
```

Source icons are `Icons/<Category>/<Name>.png`; animated icons are a folder with `frame_rate`
and `frame_NN.png`. Run the packer's tests with `python3 -m unittest scripts/asset_packer_test.py`.
