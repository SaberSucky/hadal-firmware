/**
 * @file desktop_keybinds.h
 * @brief What each button does on the desktop, pressed or held: kept in a text file on the SD
 * card, one line a binding, e.g.
 *
 *     up press: Lock Menu
 *     left hold: /ext/subghz/garage.sub
 *     ok hold: Infrared
 *     down hold: Clock
 *
 * The action is a desktop action (Main Menu, Lock Menu, Lock (with the PIN, if one is set),
 * Lock Keypad (no PIN asked), Archive, Power Off, Nothing), an
 * app's name, an app on the SD card, or any file, opened in the app it belongs to. A button that
 * has no line does what it always did. "control 1" and "control 2" are the two app tiles of the
 * control centre (Up on the desktop), bound the same way.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DESKTOP_KEYBINDS_PATH "/ext/apps_data/desktop/keybinds.txt"

typedef enum {
    DesktopKeybindUpPress,
    DesktopKeybindUpHold,
    DesktopKeybindDownPress,
    DesktopKeybindDownHold,
    DesktopKeybindLeftPress,
    DesktopKeybindLeftHold,
    DesktopKeybindRightPress,
    DesktopKeybindRightHold,
    DesktopKeybindOkPress,
    DesktopKeybindOkHold,
    /** The buttons are the binds above; the two below are the control centre's app tiles */
    DesktopKeybindButtonCount,
    DesktopKeybindControl1 = DesktopKeybindButtonCount,
    DesktopKeybindControl2,
    DesktopKeybindCount,
} DesktopKeybind;

typedef enum {
    DesktopKeybindActionNone, /**< not a desktop action: an app or a file */
    DesktopKeybindActionMainMenu,
    DesktopKeybindActionLockMenu,
    DesktopKeybindActionLock,
    DesktopKeybindActionArchive,
    DesktopKeybindActionPowerOff,
    DesktopKeybindActionNothing,
    DesktopKeybindActionLockKeypad, /**< locked, no PIN asked to unlock (the control centre's) */
} DesktopKeybindAction;

/** "up press", "ok hold": the line's start for a binding */
const char* desktop_keybind_name(DesktopKeybind bind);

/**
 * One line of the file: the binding and where its action starts (spaces trimmed, *action_length
 * long). false for a line that is not a binding
 */
bool desktop_keybinds_parse_line(
    const char* line,
    size_t length,
    DesktopKeybind* bind,
    const char** action,
    size_t* action_length);

/** A desktop action's word, case not minding ("lock menu"), or DesktopKeybindActionNone */
DesktopKeybindAction desktop_keybind_action(const char* action);

/** The app that opens a file, by where it is and what it ends in ("x.sub": "Sub-GHz"), or NULL */
const char* desktop_keybind_app_for_file(const char* path);

#ifdef __cplusplus
}
#endif
