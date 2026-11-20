# Asset packs

An asset pack replaces Hadal's animations, icons and fonts with files from the SD card, without
reflashing. Pick one in **Settings → Hadal → Asset Pack**, which lists the packs on the card, says what is in each (how many animations, icons and fonts) and applies the one you choose at once. With no pack selected nothing changes,
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
An icon that neither Hadal nor any app uses simply never shows.

## What a pack can change

- **Animations**: the desktop idle animations. If the pack has `Anims/manifest.txt` they come from
  there instead of `/ext/dolphin`; the firmware's own system animations (SD card messages and so on)
  are always available. A new set starts at once if an animation is playing, otherwise when you
  next return to the desktop.
- **Icons**: on the screens the firmware itself draws (the status bar, the desktop, the archive,
  the on-screen keyboard, the PIN and power screens), and in the apps on the SD card: an app
  carries its own copy of each icon it uses, and when it starts Hadal looks those up by name
  (`I_DolphinSuccess_91x55` takes the pack's `DolphinSuccess_91x55`, whatever folder it is in).
  The main menu's icons are the apps' own and are never replaced.
- **Passport**: `Passport/passport_128x64` is the passport's whole background, drawn in the layout
  packs are made for (the portrait at 11,2; the name, mood and level from x 59; the experience bar
  inside 58,44 to 122,50). A pack with one portrait for each mood (`passport_happy_46x49`,
  `passport_okay_46x49`, `passport_bad_46x49`) has it shown at every level.
- **Fonts**: every screen of every app, because text is drawn by the firmware.

## Matching icons

An icon is matched by its path relative to `Icons/`, without the extension, for example
`StatusBar/Battery_26x8`; an app's icons by the file name alone. Names end in the icon size; a
replacement may be another size (up to 128×64), but an animation **must have the same frame
count**, otherwise it is skipped. An icon the firmware does not have is kept for apps (up to 64
of them). Icons that are drawn with a mask
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

Icons from a pack are held in RAM: at most 24 KB in total, and at most 128×64 pixels and 32
frames each. Fonts are at most 6 KB each and 12 KB together. Whatever does not fit is skipped, and
the log says why.

## While an app runs

A pack whose icons take 12 KB of RAM or less keeps them while an app runs, so they show in the
app too. A larger one frees them when an app starts, so that the app has the memory, and loads
them again when you return to the desktop; while that app runs everything shows the built-in
icons. Fonts and animations stay as they are either way. Looking an app's icons up adds a moment
to its start, and only when the pack has icons.

## If something goes wrong

- A pack folder that is missing, empty or full of invalid files simply changes nothing.
- A pack stays "on probation" until the firmware has run with it for ten seconds. If the Flipper
  resets before that, the next start turns the pack off (Settings → Hadal → Interface → Asset Pack goes back
  to None) instead of crashing again. This also means that a deliberate reset in the ten seconds
  after choosing a pack will switch it off; just choose it again.
- Delete the pack's folder from the SD card to remove it.

## The Hadal pack

Hadal ships one pack, installed to `/ext/asset_packs/Hadal` with the firmware's resources
(`applications/main/hadal_resources`). It is **generated**, not drawn by hand, from the scripts in
`assets/hadal_pack/`:

| File | Makes |
|---|---|
| `anims.py` | the idle animations: an anglerfish (it talks, with speech bubbles), a jellyfish, a bathyscaphe and a hydrothermal vent. The scenes loop without a jump |
| `icons.py` | the status bar icons, the 13 main menu icons and the loading spinner |
| `boot.py` | the first-start and "firmware updated" slideshows (they are built into the firmware, not part of the pack) |
| `fontstyle.py` | a lighter `Primary` font and instrument-panel `BigNumbers`; the other three fonts stay the firmware's |
| `build.py` | puts it all together, validates it and installs it |

```
python3 assets/hadal_pack/build.py --install              # rewrite the pack and the slideshow frames
python3 assets/hadal_pack/build.py --previews sheets/     # also write contact sheets to look at
```

The fonts are drawn from the firmware's own glyphs and keep every glyph's box and advance, so text
takes the same room as before. To go back to the standard look for one of them, delete its file
from `Fonts/` on the card. `scripts/flipper/assets/u8g2font.py` reads and writes u8g2 fonts, if you
want to restyle the others.

## Building a pack

`scripts/asset_packer.py` builds a pack from PNG sources laid out like `assets/icons`:

```
python3 scripts/asset_packer.py pack  <source> <output>   # source has Anims/, Icons/ and/or Fonts/
python3 scripts/asset_packer.py validate <pack>           # checks it the way the firmware will
python3 scripts/asset_packer.py preview  <pack> sheet.png # renders the icons
```

Source icons are `Icons/<Category>/<Name>.png`; animated icons are a folder with `frame_rate`
and `frame_NN.png`. Run the packer's tests with `python3 -m unittest scripts/asset_packer_test.py`.
