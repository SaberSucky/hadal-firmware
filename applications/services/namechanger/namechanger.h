#pragma once

#define NAMECHANGER_HEADER  "Flipper Name File"
#define NAMECHANGER_VERSION 1
#define NAMECHANGER_PATH    EXT_PATH("dolphin/name.settings")

/* Spoofing, set in Settings > Hadal > Misc > Spoofing and applied at start: an optional "Mac" (6
 * bytes, as written: most significant first) and an optional "Serial" (8 bytes) */
#define HADAL_SPOOF_HEADER  "Hadal Spoof File"
#define HADAL_SPOOF_VERSION 1
#define HADAL_SPOOF_PATH    EXT_PATH("apps_data/spoof/spoof.txt")
#define HADAL_SPOOF_MAC     "Mac"
#define HADAL_SPOOF_SERIAL  "Serial"

/** Created (data unused) once the name and spoofing above are applied, or found absent. The desktop
 * waits for it before bringing up the USB serial port, so that port appears once, with its final
 * name: qFlipper loses track of a Flipper whose port comes and goes while it is reading it. */
#define RECORD_NAMECHANGER_DONE "namechanger_done"
