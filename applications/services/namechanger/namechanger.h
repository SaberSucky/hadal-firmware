#pragma once

#define NAMECHANGER_HEADER  "Flipper Name File"
#define NAMECHANGER_VERSION 1
#define NAMECHANGER_PATH    EXT_PATH("dolphin/name.settings")

/* Spoofing, set in Settings > Hadal > Spoofing and applied at start: the identity chosen there,
 * as top-level keys. An optional "Mac" (6 bytes, as written: most significant first) or "Random
 * mac", an optional "Serial" (8 bytes), an optional "Name". The profiles under them are the
 * page's own */
#define HADAL_SPOOF_HEADER  "Hadal Spoof File"
#define HADAL_SPOOF_VERSION 2 /* 1 had Mac and Serial alone; still read */
#define HADAL_SPOOF_PATH    EXT_PATH("apps_data/spoof/spoof.txt")
#define HADAL_SPOOF_MAC     "Mac"
#define HADAL_SPOOF_SERIAL  "Serial"
#define HADAL_SPOOF_RANDOM  "Random mac" /**< true: a new random address at every start */
#define HADAL_SPOOF_NAME    "Name" /**< the name to start as, over the one Desktop sets */
/** Counts the changes made; the start shares the one it applied as RECORD_NAMECHANGER_DONE's
 * byte, so the settings know whether a restart is still to come */
#define HADAL_SPOOF_GENERATION "Generation"

/** Created (its byte the Generation applied) once the name and spoofing above are applied, or found absent. The desktop
 * waits for it before bringing up the USB serial port, so that port appears once, with its final
 * name: qFlipper loses track of a Flipper whose port comes and goes while it is reading it. */
#define RECORD_NAMECHANGER_DONE "namechanger_done"
