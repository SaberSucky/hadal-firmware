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
