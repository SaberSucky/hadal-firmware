#pragma once

/**
 * The control centre: Up on the desktop. Tiles for the things people switch often, the
 * brightness and volume as sliders, and the time and battery along the top.
 *
 *   12:05                   87% [==]
 *   [lock][pin ][ bt ][mute][dark]  |b| |v|
 *   [flip][hadl][app1][app2][dumy]  | | | |
 *            Bluetooth: on
 *
 * The arrows move between tiles and sliders, OK switches a tile or opens it, Up and Down move
 * a slider that has the focus, and OK on the volume slider turns vibration on or off.
 */

#include <gui/view.h>
#include "desktop_events.h"

typedef enum {
    ControlCentreTileLock,
    ControlCentreTilePinLock,
    ControlCentreTileBluetooth,
    ControlCentreTileStealth,
    ControlCentreTileDark,
    ControlCentreTileLeftHanded,
    ControlCentreTileHadal,
    ControlCentreTileApp1,
    ControlCentreTileApp2,
    ControlCentreTileDummy,
    ControlCentreTileCount,
} ControlCentreTile;

#define CONTROL_CENTRE_APP_LABEL_SIZE 22

/** What the control centre shows: kept by the scene, drawn by the view */
typedef struct {
    bool on[ControlCentreTileCount]; // a switch that is on is drawn filled
    bool pin_set;
    float brightness; // 0..1, as in the notification settings
    float volume;
    bool vibro;
    char app_label[2][CONTROL_CENTRE_APP_LABEL_SIZE]; // empty: no app chosen
    uint8_t hour;
    uint8_t minute;
    bool format_12;
    uint8_t battery; // percent
    bool charging;
} ControlCentreState;

typedef struct DesktopControlCentreView DesktopControlCentreView;

typedef void (*DesktopControlCentreCallback)(DesktopEvent event, void* context);

DesktopControlCentreView* desktop_control_centre_alloc(void);
void desktop_control_centre_free(DesktopControlCentreView* control_centre);
View* desktop_control_centre_get_view(DesktopControlCentreView* control_centre);

void desktop_control_centre_set_callback(
    DesktopControlCentreView* control_centre,
    DesktopControlCentreCallback callback,
    void* context);

/** Show this state; the focus stays where it is */
void desktop_control_centre_set_state(
    DesktopControlCentreView* control_centre,
    const ControlCentreState* state);

/** The state as the view holds it: slider values the user moved, the tile last chosen */
void desktop_control_centre_get_state(
    DesktopControlCentreView* control_centre,
    ControlCentreState* state);

/** The tile OK was pressed on, for DesktopControlCentreEventTile */
ControlCentreTile desktop_control_centre_get_tile(DesktopControlCentreView* control_centre);

/** Focus the first tile */
void desktop_control_centre_reset_focus(DesktopControlCentreView* control_centre);
