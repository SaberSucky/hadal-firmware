#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <storage/storage.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HADAL_PACKS_PATH     "/ext/asset_packs"
/** Pack folder names are at most this long, terminator included (the desktop setting's size). */
#define HADAL_PACK_NAME_SIZE 32

/** What an asset pack folder holds, as the firmware would find it. */
typedef struct {
    uint16_t animations; /**< entries in Anims/manifest.txt */
    uint16_t icons; /**< .bmx files under Icons, and animated icon folders there */
    uint8_t fonts; /**< .u8f files in Fonts */
} HadalPackInfo;

/**
 * Names of the pack folders in /ext/asset_packs, sorted without regard to case, at most `max`.
 * Hidden folders and names the firmware would refuse are left out.
 * @return how many names were written
 */
size_t hadal_pack_list(Storage* storage, char (*names)[HADAL_PACK_NAME_SIZE], size_t max);

/** Count what the pack called `name` holds. Missing parts count as zero. */
void hadal_pack_info_scan(Storage* storage, const char* name, HadalPackInfo* info);

#ifdef __cplusplus
}
#endif
