#pragma once

typedef enum {
    DesktopMainEventLock,
    DesktopMainEventOpenLockMenu,
    DesktopMainEventOpenArchive,
    DesktopMainEventOpenFavoriteLeftShort,
    DesktopMainEventOpenFavoriteLeftLong,
    DesktopMainEventOpenFavoriteRightShort,
    DesktopMainEventOpenFavoriteRightLong,
    DesktopMainEventOpenFavoriteOkLong,
    DesktopMainEventOpenMenu,
    DesktopMainEventOpenDebug,
    DesktopMainEventOpenPowerOff,

    DesktopDummyEventOpenLeft,
    DesktopDummyEventOpenDown,
    DesktopDummyEventOpenOk,
    DesktopDummyEventOpenUpLong,
    DesktopDummyEventOpenDownLong,
    DesktopDummyEventOpenLeftLong,
    DesktopDummyEventOpenRightLong,
    DesktopDummyEventOpenOkLong,

    DesktopLockedEventUnlocked,
    DesktopLockedEventUpdate,
    DesktopLockedEventShowPinInput,
    DesktopLockedEventDoorsClosed,

    DesktopPinInputEventResetWrongPinLabel,
    DesktopPinInputEventUnlocked,
    DesktopPinInputEventUnlockFailed,
    DesktopPinInputEventBack,

    DesktopPinTimeoutExit,

    DesktopDebugEventToggleDebugMode,
    DesktopDebugEventExit,

    DesktopControlCentreEventNone,
    DesktopControlCentreEventTile, /**< OK on a tile: desktop_control_centre_get_tile() */
    DesktopControlCentreEventBrightness,
    DesktopControlCentreEventVolume,
    DesktopControlCentreEventVibro,
    DesktopControlCentreEventChangeApp, /**< OK held on an app tile: desktop_control_centre_get_tile() */
    DesktopControlCentreEventPick, /**< OK in the app picker: desktop_control_centre_get_pick() */
    DesktopControlCentreEventPickCancel, /**< Back in the app picker */

    DesktopAnimationEventCheckAnimation,
    DesktopAnimationEventNewIdleAnimation,
    DesktopAnimationEventInteractAnimation,

    DesktopSlideshowCompleted,
    DesktopSlideshowPoweroff,

    DesktopHwMismatchExit,

    DesktopEnclaveExit,

    // Global events
    DesktopGlobalBeforeAppStarted,
    DesktopGlobalAfterAppFinished,
    DesktopGlobalAutoLock,
    DesktopGlobalApiUnlock,
    DesktopGlobalSaveSettings,
    DesktopGlobalReloadSettings,
    DesktopGlobalAssetPacksChanged,
    DesktopGlobalAssetPackStable,
    DesktopGlobalBootStable,
    DesktopGlobalSaveCrashReport,

    /** A desktop button pressed or held: this + DesktopKeybind (helpers/desktop_keybinds.h) */
    DesktopMainEventKeybind,
    DesktopMainEventKeybindEnd = DesktopMainEventKeybind + 10,
} DesktopEvent;
