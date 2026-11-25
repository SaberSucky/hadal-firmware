#pragma once

#include "power.h"

#include <gui/gui.h>
#include <gui/view_holder.h>

#include <toolbox/api_lock.h>
#include <assets_icons.h>

#include "battery_log.h"
#include "views/power_off.h"
#include "views/power_unplug_usb.h"

typedef enum {
    PowerStateNotCharging,
    PowerStateCharging,
    PowerStateCharged,
} PowerState;

struct Power {
    ViewHolder* view_holder;
    FuriPubSub* event_pubsub;
    FuriEventLoop* event_loop;
    FuriMessageQueue* message_queue;

    ViewPort* battery_view_port;
    PowerOff* view_power_off;
    PowerUnplugUsb* view_power_unplug_usb;

    PowerEvent event;
    PowerState state;
    PowerInfo info;

    bool battery_low;
    bool show_battery_low_warning;
    uint8_t displayBatteryPercentage;
    bool is_otg_requested;
    uint8_t ticks_since_full_read; /**< power_update_info(): seconds since all was read */
    uint8_t battery_level;
    uint8_t power_off_timeout;
    PowerSettings settings;
    FuriTimer* auto_poweroff_timer;
    bool app_running;
    FuriPubSub* input_events_pubsub;
    FuriPubSubSubscription* input_events_subscription;
    uint8_t charge_held_at; /**< Limit charge: the cap charging is held off at, 0 when it isn't */
    BatteryLog battery_log;
};

/** Called every second: writes a battery history record when one is due */
void battery_log_tick(BatteryLog* log, const PowerInfo* info);

typedef enum {
    PowerViewOff,
    PowerViewUnplugUsb,
} PowerView;

typedef enum {
    PowerMessageTypeShutdown,
    PowerMessageTypeReboot,
    PowerMessageTypeGetInfo,
    PowerMessageTypeIsBatteryHealthy,
    PowerMessageTypeShowBatteryLowWarning,
    PowerMessageTypeGetSettings,
    PowerMessageTypeSetSettings,
    PowerMessageTypeReloadSettings,
    PowerMessageTypeSwitchOTG,
} PowerMessageType;

typedef struct {
    PowerMessageType type;
    union {
        PowerBootMode boot_mode;
        PowerInfo* power_info;
        bool* bool_param;
        PowerSettings* settings;
        const PowerSettings* csettings;
    };
    FuriApiLock lock;
} PowerMessage;
