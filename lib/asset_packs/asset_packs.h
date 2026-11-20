/**
 * @file asset_packs.h
 * Runtime asset packs: icons and fonts loaded from /ext/asset_packs/<Name>/.
 *
 * With no pack active nothing changes: every call below is a no-op and drawing takes the
 * original code path. Anything a pack does not provide, or provides wrongly, is simply not
 * replaced. See documentation/AssetPacks.md.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <gui/icon.h>

#include "asset_pack_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ASSET_PACKS_PATH     "/ext/asset_packs"
/** Size of a pack name buffer, terminator included. */
#define ASSET_PACK_NAME_SIZE 32
/** How long a replaced pack font stays in memory before it is freed. */
#ifndef ASSET_PACKS_FONT_GRACE_MS
#define ASSET_PACKS_FONT_GRACE_MS 3000
#endif

/** Called after the set of replaced assets changed, from the thread that changed it. */
typedef void (*AssetPacksChangedCallback)(void* context);

/**
 * Make a pack the active one, or none with NULL or "". Icons are loaded in the background;
 * until that finishes, and whenever loading fails, the built-in icons are used.
 * Not thread safe against itself: call from one thread (the desktop service).
 */
void asset_packs_set_active(const char* name);

/** Name of the active pack, empty if none. */
void asset_packs_get_active(char* name, size_t size);

/** Free the active pack's icons, e.g. to give a big app the RAM. Drawing falls back to built-in. */
void asset_packs_unload_icons(void);

/** Whether a pack is loading now (its memory is in use and changing) */
bool asset_packs_is_loading(void);

/** Load the active pack's icons again after asset_packs_unload_icons(). No-op if not unloaded. */
void asset_packs_reload_icons(void);

/** Most RAM (icons and fonts) a pack may hold to stay while an app runs */
#define ASSET_PACK_APP_KEEP_RAM (6u * 1024u)

/**
 * An app is about to start: the pack stays if it is small (ASSET_PACK_APP_KEEP_RAM, icons and
 * fonts together), so that it shows in apps too, else its icons and fonts are freed to give the
 * app the RAM. asset_packs_reload_icons() brings them back afterwards.
 */
void asset_packs_app_starting(void);

typedef struct FlipperApplication FlipperApplication;

/**
 * An app from the SD card is being loaded: its own copies of icons (I_<name>, A_<name>) are drawn
 * as the pack's icons of the same file name while it runs. Reads the app's symbol table, so call
 * it after flipper_application_preload() and before flipper_application_map_to_memory(); does
 * nothing when the pack has no icons loaded.
 */
void asset_packs_app_loaded(FlipperApplication* app);

/** The app is about to be freed: stop replacing its icons. Call before its memory goes. */
void asset_packs_app_unloaded(void);

/**
 * True if the firmware died while a pack was loading or soon after (see asset_packs_mark_stable()),
 * i.e. the pack is the likely cause. Clears that state, so it answers true only once. Call
 * before asset_packs_set_active() at boot, and if it says yes, do not select the pack again.
 */
bool asset_packs_previous_load_crashed(void);

/**
 * The firmware has run with the loaded pack long enough: end the crash watch. Until then a
 * pack that was installed is on probation, and a reset counts against it. Does nothing while a
 * load is still running, which will announce itself through the changed callback.
 */
void asset_packs_mark_stable(void);

void asset_packs_set_changed_callback(AssetPacksChangedCallback callback, void* context);

/**
 * The icon to draw in place of `icon`: a pack's replacement, or `icon` itself.
 *
 * If this returns something other than `icon`, the pack is locked and the caller MUST call
 * asset_packs_icon_release() as soon as it has finished drawing, and must not keep the result.
 * With no pack active this is a single flag check.
 */
const Icon* asset_packs_icon_acquire(const Icon* icon);

void asset_packs_icon_release(void);

/**
 * Tell the manager which built-in font each slot uses (the canvas's Font enum order,
 * ASSET_PACK_FONT_SLOTS entries). Pack fonts are checked against these fonts' layout metrics.
 * Call once at start-up, before a pack is loaded; the array must stay valid.
 */
void asset_packs_set_stock_fonts(const uint8_t* const* fonts);

/**
 * The u8g2 font to use for a slot: the active pack's replacement, or `stock`. Lock-free, one
 * pointer read when no pack font is loaded.
 *
 * The result stays valid for ASSET_PACKS_FONT_GRACE_MS after the pack changes, which is far
 * longer than a frame takes to draw. A canvas only reads its font while drawing, and every draw
 * starts by selecting a font again, so none keeps a stale pointer.
 */
const uint8_t* asset_packs_font(unsigned slot, const uint8_t* stock);

#ifdef __cplusplus
}
#endif
