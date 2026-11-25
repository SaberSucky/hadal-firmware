# Frequency Atlas band plans {#frequency_atlas_format}

Remote Analyzer > Frequency Atlas lists what each sub-GHz band is used for in a region, its
legal limit (for reference only, not a compliance authority) and whether anything is on the air
there now. The band plans are plain text files on the SD card, one per region:

    /ext/frequency_atlas/<code>.txt

`<code>` is 1 to 15 characters out of `a-z`, `0-9`, `_` and `-`. Hadal ships `uk.txt`, `eu.txt`
and `us.txt` (source: `applications/system/remote_analyzer/resources/frequency_atlas/`,
deployed with the firmware's SD card resources). Settings > Hadal > Protocols > Region picks
UK, EU, US or Off; holding OK in the atlas lists every valid file in the folder, so a new region
needs only a new file, no firmware build. The choice is kept in
`/ext/apps_data/remote_analyzer/atlas_region` (`Region: <code>` or `Region: off`).

A firmware update puts the shipped files back as they were: to change one, copy it to a new name
and edit the copy. Files the update did not ship are left alone.

## Format

    Filetype: Flipper Frequency Atlas
    Version: 1
    Region: UK
    Name: UK - Ofcom IR 2030
    Source: Ofcom IR 2030 (licence exempt SRDs), published 23 Oct 2026
    Note: Reference only, not legal advice or a compliance check.
    # start_hz | end_hz | listen_hz | label | typical use | limit
    Band: 433050000|434790000|433920000|SRD 433|Remotes, sensors|10 mW e.r.p., 10% duty

- `Filetype` and `Version` are required; a file without them is not shown.
- `Region` is the short name in the atlas's header (the file name in capitals when absent).
  `Name` is the picker's entry, `Source` and `Note` close every band's details screen.
- One `Band` line per band, six fields split by `|`, spaces around them ignored:
  start and end in Hz (whole numbers, start below end), the frequency the receiver parks on to
  check the band (empty for the middle; it must lie within the band), a label (required, the
  list shows its first ~16 characters), what the band is typically used for, and the limit.
- Lines starting with `#` are comments, and keys the atlas does not know are skipped.
- A `Band` line that does not parse is skipped; the rest of the file still loads. A file with no
  band that parses, or over 16 KB, is not shown. At most 32 bands are read.
- Keep to ASCII: the Flipper's fonts have no other characters.

List only bands the CC1101 can tune (300-348, 386-464 and 778-928 MHz, or 281-361, 378-481 and
749-962 MHz with Extended Sub-GHz). A band whose listen frequency the radio cannot tune right now
is shown as `n/a` and skipped by the scan.

## How the live reading works

One receiver, so the atlas parks on one band at a time with the Read screen's modulation and
trigger level, about 0.2 s each while the air is quiet. A band with a carrier keeps the receiver
for up to 3 s, so the decoders hear the whole press; Right holds it on the selected band
indefinitely, Left goes back to scanning. A band reads `now` for 4 s after its last carrier, then
how long ago it was heard. A protocol that decodes is named under the band (and marked `*` in the
list). The receiver listens wider than the narrowest sub-bands, so neighbours can share a reading.
