#include <furi.h>
#include <furi_hal.h>

#include <gui/gui.h>
#include <gui/elements.h>

#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <notification/notification_app.h>

#include <storage/storage.h>
#include <toolbox/saved_struct.h>

#include "clock_app.h"

static uint32_t alarm_minutes_until(AppState* app);
static void alarm_until_text(uint32_t minutes, char* out, size_t size, bool compact);

/*
    A modified clock app for use overnight.
    Up / Down controls the display brightness. Down at brightness 0 toggles the
    notification LED. Right opens the alarm setup.

    The alarm is the Flipper's one alarm, the same as Settings > Clock & Alarm:
    kept by the clock chip, it rings from anywhere (this app open or not) on the
    one alarm screen, with snooze. Set here or there, it is the same alarm.
*/

int brightness = 5;
bool led = false;
NotificationApp* notif = 0;

// How long the brightness bar stays on screen after Up/Down.
#define BRIGHTNESS_OSD_MS 3000

const NotificationMessage message_red_dim = {
    .type = NotificationMessageTypeLedRed,
    .data.led.value = 0xFF / 16,
};

const NotificationMessage message_red_off = {
    .type = NotificationMessageTypeLedRed,
    .data.led.value = 0x00,
};

static const NotificationSequence led_on = {
    &message_red_dim,
    &message_do_not_reset,
    NULL,
};

static const NotificationSequence led_off = {
    &message_red_off,
    &message_do_not_reset,
    NULL,
};

static const NotificationSequence led_reset = {
    &message_red_0,
    NULL,
};

// Use force_on (not backlight_on) so the level is pushed to the panel even when
// the backlight is already on - a plain backlight_on early-returns in that case.
// Used on exit to restore the saved brightness after the enforce lock is freed.
void set_backlight_brightness(float brightness) {
    notif->settings.display_brightness = brightness;
    notification_message(notif, &sequence_display_backlight_force_on);
}

// While the app holds the backlight on with enforce_on, a plain backlight_on
// does not change the screen: the notification service early-returns when the
// backlight is already on, and the enforced "internal" layer that is actually
// shown is only set once at startup. force_on bypasses that early-return to
// update the panel now; the enforce pair then refreshes the locked internal
// layer so the new level cannot be reverted.
void set_enforced_brightness(float brightness) {
    notif->settings.display_brightness = brightness;
    notification_message(notif, &sequence_display_backlight_force_on);
    notification_message(notif, &sequence_display_backlight_enforce_auto);
    notification_message(notif, &sequence_display_backlight_enforce_on);
}

static void show_brightness_osd(AppState* app) {
    app->brightness_shown_until = furi_get_tick() + furi_ms_to_ticks(BRIGHTNESS_OSD_MS);
}

void handle_up(AppState* app) {
    show_brightness_osd(app);
    if(brightness < 100) {
        led = false;
        notification_message(notif, &led_off);
        brightness += 5;
        if(brightness > 100) brightness = 100;
    }
    set_enforced_brightness((float)(brightness / 100.f));
}

void handle_down(AppState* app) {
    show_brightness_osd(app);
    if(brightness > 0) {
        brightness -= 5;
        if(brightness < 0) brightness = 0;
        if(brightness == 0) { //trigger only on the first brightness 5 -> 0 transition
            led = true;
            notification_message(notif, &led_on);
        }
    } else if(brightness == 0) { //trigger on every down press afterwards
        led = !led;
        if(led) {
            notification_message(notif, &led_on);
        } else {
            notification_message(notif, &led_off);
        }
    }
    set_enforced_brightness((float)(brightness / 100.f));
}

//do you are have stupid?
// idk who left that comment here, but i don't have stupid sorry
void elements_progress_bar_vertical(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    uint8_t height,
    float progress) {
    furi_assert(canvas);
    furi_assert(((float)progress >= 0.0f) && ((float)progress <= 1.0f));

    uint8_t width = 9;

    uint8_t progress_length = roundf((1.f - progress) * (height - 2));

    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, x + 1, y + 1, width - 2, height - 2);

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, x + 1, y + 1, width - 2, progress_length);

    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, x, y, width, height, 3);
}

// The alarm is stored as 24h internally; these convert for locale-aware display
// and editing (12h + AM/PM when the system time format is 12h).
static void to_12h(uint8_t h24, uint8_t* h12, bool* pm) {
    *pm = h24 >= 12;
    uint8_t h = h24 % 12;
    *h12 = (h == 0) ? 12 : h;
}

static uint8_t to_24h(uint8_t h12, bool pm) {
    return (h12 % 12) + (pm ? 12 : 0); // 12 -> 0, so 12AM=00:00, 12PM=12:00
}

static void
    format_alarm_time(char* buf, size_t n, uint8_t h24, uint8_t minute, LocaleTimeFormat fmt) {
    if(fmt == LocaleTimeFormat12h) {
        uint8_t h12;
        bool pm;
        to_12h(h24, &h12, &pm);
        snprintf(buf, n, "%u:%.2u %s", h12, minute, pm ? "PM" : "AM");
    } else {
        snprintf(buf, n, "%.2u:%.2u", h24, minute);
    }
}

static void clock_render_callback(Canvas* const canvas, void* ctx) {
    ClockModel* model = ctx;
    AppState* state = model->app;

    if(furi_get_tick() < state->brightness_shown_until) {
        elements_progress_bar_vertical(canvas, 119, 1, 62, (float)(brightness / 100.f));
    }

    DateTime curr_dt;
    furi_hal_rtc_get_datetime(&curr_dt);
    uint32_t curr_ts = datetime_datetime_to_timestamp(&curr_dt);

    char time_string[TIME_LEN];
    char date_string[DATE_LEN];
    char meridian_string[MERIDIAN_LEN];
    char date_pct_string[DATE_PCT_LEN];
    char timer_string[20];

    if(state->time_format == LocaleTimeFormat24h) {
        snprintf(
            time_string, TIME_LEN, CLOCK_TIME_FORMAT, curr_dt.hour, curr_dt.minute, curr_dt.second);
    } else {
        bool pm12 = curr_dt.hour >= 12;
        uint8_t hour12 = curr_dt.hour % 12;
        if(hour12 == 0) hour12 = 12;
        snprintf(time_string, TIME_LEN, CLOCK_TIME_FORMAT, hour12, curr_dt.minute, curr_dt.second);

        snprintf(
            meridian_string,
            MERIDIAN_LEN,
            MERIDIAN_FORMAT,
            pm12 ? MERIDIAN_STRING_PM : MERIDIAN_STRING_AM);
    }

    if(state->date_format == LocaleDateFormatYMD) {
        snprintf(
            date_string, DATE_LEN, CLOCK_ISO_DATE_FORMAT, curr_dt.year, curr_dt.month, curr_dt.day);
    } else if(state->date_format == LocaleDateFormatMDY) {
        snprintf(
            date_string, DATE_LEN, CLOCK_RFC_DATE_FORMAT, curr_dt.month, curr_dt.day, curr_dt.year);
    } else {
        snprintf(
            date_string, DATE_LEN, CLOCK_RFC_DATE_FORMAT, curr_dt.day, curr_dt.month, curr_dt.year);
    }

    bool timer_running = state->timer_running;
    uint32_t timer_start_timestamp = state->timer_start_timestamp;
    uint32_t timer_stopped_seconds = state->timer_stopped_seconds;

    canvas_set_font(canvas, FontBigNumbers);

    if(timer_start_timestamp != 0) {
        int32_t elapsed_secs = timer_running ? (curr_ts - timer_start_timestamp) :
                                               timer_stopped_seconds;
        snprintf(timer_string, 20, "%.2ld:%.2ld", elapsed_secs / 60, elapsed_secs % 60);
        if(state->time_format == LocaleTimeFormat12h) {
            canvas_draw_str_aligned(
                canvas, 56, 8, AlignCenter, AlignCenter, time_string); // DRAW TIME
        } else {
            canvas_draw_str_aligned(
                canvas, 64, 8, AlignCenter, AlignCenter, time_string); // DRAW TIME
        }
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignTop, timer_string); // DRAW TIMER
        canvas_set_font(canvas, FontSecondary);
        if(state->time_format == LocaleTimeFormat12h) {
            canvas_draw_str_aligned(canvas, 112, 8, AlignCenter, AlignCenter, meridian_string);
        }

        snprintf(
            date_pct_string, sizeof(date_pct_string), "%s   %u%%", date_string, state->battery_pct);
        canvas_draw_str_aligned(
            canvas, 64, 20, AlignCenter, AlignTop, date_pct_string); // DRAW DATE + BATTERY
        elements_button_left(canvas, "Reset");
    } else {
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, time_string);
        canvas_set_font(canvas, FontSecondary);

        if(state->time_format == LocaleTimeFormat12h) {
            snprintf(
                date_pct_string,
                sizeof(date_pct_string),
                "%s   %u%%",
                date_string,
                state->battery_pct);
            canvas_draw_str_aligned(canvas, 64, 17, AlignCenter, AlignCenter, date_pct_string);
            canvas_draw_str_aligned(canvas, 64, 48, AlignCenter, AlignCenter, meridian_string);
        } else {
            canvas_draw_str_aligned(canvas, 64, 17, AlignCenter, AlignCenter, date_string);
            snprintf(date_pct_string, sizeof(date_pct_string), "%u%%", state->battery_pct);
            canvas_draw_str_aligned(canvas, 64, 48, AlignCenter, AlignCenter, date_pct_string);
        }
    }
    if(timer_running) {
        elements_button_center(canvas, "Stop");
    } else if(timer_start_timestamp != 0 && !timer_running) {
        elements_button_center(canvas, "Start");
    }

    // A small bell + alarm time on the left when armed, so the user can see the
    // alarm is set without opening the menu. Kept out of the timer layout to
    // avoid colliding with the Reset button.
    if(state->settings.alarm_enabled && timer_start_timestamp == 0) {
        char alarm_time[12];
        format_alarm_time(
            alarm_time,
            sizeof(alarm_time),
            state->settings.alarm_hour,
            state->settings.alarm_minute,
            state->time_format);
        // a bell, then the time it rings
        static const char* const bell[] = {
            "...#...", "..###..", ".#####.", ".#####.", ".#####.", "#######", ".......", "...#..."};
        for(uint8_t row = 0; row < COUNT_OF(bell); row++) {
            for(uint8_t col = 0; bell[row][col]; col++) {
                if(bell[row][col] == '#') canvas_draw_dot(canvas, 2 + col, 55 + row);
            }
        }
        canvas_set_font(canvas, FontSecondary);
        char until[16];
        alarm_until_text(alarm_minutes_until(state), until, sizeof(until), true);
        char line[40];
        snprintf(line, sizeof(line), "%s  in %s", alarm_time, until);
        canvas_draw_str_aligned(canvas, 11, 62, AlignLeft, AlignBottom, line);
    }
    if(timer_start_timestamp == 0) {
        // Right: where the alarm is set
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 126, 62, AlignRight, AlignBottom, "Alarm >");
    }
}

static void timer_start_stop(AppState* plugin_state) {
    uint32_t curr_ts = furi_hal_rtc_get_timestamp();
    if(plugin_state->timer_running) {
        plugin_state->timer_stopped_seconds = curr_ts - plugin_state->timer_start_timestamp;
    } else {
        if(plugin_state->timer_start_timestamp == 0) {
            plugin_state->timer_start_timestamp = curr_ts;
        } else {
            plugin_state->timer_start_timestamp = curr_ts - plugin_state->timer_stopped_seconds;
        }
    }
    plugin_state->timer_running = !plugin_state->timer_running;
}

static void timer_reset_seconds(AppState* plugin_state) {
    if(plugin_state->timer_start_timestamp != 0) {
        plugin_state->timer_running = false;
        plugin_state->timer_start_timestamp = 0;
        plugin_state->timer_stopped_seconds = 0;
    }
}

static void ns_settings_save(AppState* app) {
    app->settings.brightness = (uint8_t)brightness; // keep the saved level current
    // the alarm lives on the clock chip: the file says "off" so it is never moved there again
    NsSettings saved = app->settings;
    saved.alarm_enabled = false;
    saved_struct_save(
        NS_SETTINGS_PATH, &saved, sizeof(NsSettings), NS_SETTINGS_MAGIC, NS_SETTINGS_VERSION);
}

static void ns_switch(AppState* app, NsViewId view) {
    app->current_view = view;
    view_dispatcher_switch_to_view(app->view_dispatcher, view);
}

// ---------------- clock view ----------------

static bool clock_input_callback(InputEvent* event, void* context) {
    AppState* app = context;

    if(event->type != InputTypeShort) return false;

    switch(event->key) {
    case InputKeyLeft:
        timer_reset_seconds(app);
        return true;
    case InputKeyOk:
        timer_start_stop(app);
        return true;
    case InputKeyUp:
        handle_up(app);
        return true;
    case InputKeyDown:
        handle_down(app);
        return true;
    case InputKeyRight:
        ns_switch(app, NsViewAlarmMenu);
        return true;
    case InputKeyBack:
        // Not consumed: the dispatcher's navigation callback exits the app.
        return false;
    default:
        return false;
    }
}

// ---------------- the alarm: the clock chip's, shared with Settings ----------------

/** The alarm as the clock chip has it now: Settings may have changed it */
static void ns_alarm_read(AppState* app) {
    DateTime alarm;
    app->settings.alarm_enabled = furi_hal_rtc_get_alarm(&alarm);
    app->settings.alarm_hour = alarm.hour % 24;
    app->settings.alarm_minute = alarm.minute % 60;
}

static void ns_alarm_write(AppState* app) {
    DateTime alarm = {
        .hour = app->settings.alarm_hour,
        .minute = app->settings.alarm_minute,
        .second = 0,
    };
    furi_hal_rtc_set_alarm(&alarm, app->settings.alarm_enabled);
}

// ---------------- alarm menu (VariableItemList) ----------------

static const char* const alarm_toggle_text[] = {"Off", "On"}; // as Settings > Clock & Alarm

// The page's rows, in order
enum {
    AlarmRowOn,
    AlarmRowTime,
    AlarmRowRepeat,
    AlarmRowDays,
    AlarmRowSnooze,
    AlarmRowRings,
    AlarmRowSound,
    AlarmRowGentle,
    AlarmRowVibro,
    AlarmRowLight,
};

// Repeat: the days it skips (furi_hal_rtc.h), Custom being any other set
static const char* const repeat_text[] = {"Once", "Every day", "Weekdays", "Weekends", "Custom"};
static const uint32_t repeat_skip[] = {0x7F, 0x00, 0x60, 0x1F};
#define REPEAT_CUSTOM 4

static const char* const snooze_text[] =
    {"1 min", "3 min", "5 min", "9 min", "10 min", "15 min", "20 min", "30 min"};
static const uint8_t snooze_minutes[] = {1, 3, 5, 9, 10, 15, 20, 30};
static const char* const rings_text[] =
    {"1 min", "3 min", "5 min", "10 min", "15 min", "20 min", "30 min"};
static const uint8_t rings_minutes[] = {1, 3, 5, 10, 15, 20, 30};
static const char day_letters[] = "MTWTFSS";

static void alarm_options_save(AppState* app) {
    furi_hal_rtc_set_register(FuriHalRtcRegisterAlarm, app->alarm_options);
}

static uint8_t repeat_index(uint32_t options) {
    const uint32_t skip = options & FURI_HAL_RTC_ALARM_SKIP_DAYS;
    for(uint8_t i = 0; i < COUNT_OF(repeat_skip); i++) {
        if(repeat_skip[i] == skip) return i;
    }
    return REPEAT_CUSTOM;
}

static uint8_t nearest_index(const uint8_t* values, size_t count, uint8_t value) {
    for(size_t i = 0; i < count; i++) {
        if(values[i] >= value) return i;
    }
    return count - 1;
}

/** The days it rings on, as letters, a dot for the others: "MTWTF.." */
static void days_text(uint32_t options, char* out) {
    for(uint8_t day = 0; day < 7; day++) {
        out[day] = (options & (1UL << day)) ? '.' : day_letters[day];
    }
    out[7] = '\0';
}

/** Minutes until it next rings, from now, with its days; 0 if never */
static uint32_t alarm_minutes_until(AppState* app) {
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    const int32_t from = now.hour * 60 + now.minute;
    const int32_t at = app->settings.alarm_hour * 60 + app->settings.alarm_minute;
    for(int32_t day = 0; day <= 7; day++) {
        const int32_t minutes = day * 24 * 60 + at - from;
        if(minutes <= 0) continue;
        const uint8_t weekday = ((now.weekday + 6 + day) % 7) + 1; // 1 Monday .. 7 Sunday
        if(furi_hal_rtc_alarm_rings_on(app->alarm_options, weekday)) return minutes;
    }
    return 0;
}

/** "7 h 12 min", "45 min", "2 d 3 h": how long until it rings ("7h 12m" where room is short) */
static void alarm_until_text(uint32_t minutes, char* out, size_t size, bool compact) {
    if(minutes >= 24 * 60) {
        snprintf(
            out,
            size,
            compact ? "%lud %luh" : "%lu d %lu h",
            minutes / (24 * 60),
            (minutes / 60) % 24);
    } else if(minutes >= 60) {
        snprintf(out, size, compact ? "%luh %lum" : "%lu h %lu min", minutes / 60, minutes % 60);
    } else {
        snprintf(out, size, compact ? "%lum" : "%lu min", minutes);
    }
}

/** For a moment: "Alarm set, in 7 h 12 min", then back to the page */
static void alarm_set_show(AppState* app) {
    if(!app->settings.alarm_enabled) return;
    char until[16];
    alarm_until_text(alarm_minutes_until(app), until, sizeof(until), false);
    snprintf(app->alarm_set_text, sizeof(app->alarm_set_text), "Rings in %s", until);
    popup_set_text(app->alarm_set_popup, app->alarm_set_text, 64, 36, AlignCenter, AlignCenter);
    ns_switch(app, NsViewAlarmSet);
}

static void alarm_toggle_changed(VariableItem* item) {
    AppState* app = variable_item_get_context(item);
    app->settings.alarm_enabled = variable_item_get_current_value_index(item) == 1;
    variable_item_set_current_value_text(item, alarm_toggle_text[app->settings.alarm_enabled]);
    ns_alarm_write(app);
    alarm_set_show(app);
}

static void refresh_days_item(AppState* app) {
    char text[8];
    days_text(app->alarm_options, text);
    variable_item_set_current_value_text(app->alarm_days_item, text);
}

static void repeat_changed(VariableItem* item) {
    AppState* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, repeat_text[index]);
    if(index < COUNT_OF(repeat_skip)) {
        app->alarm_options = (app->alarm_options & ~FURI_HAL_RTC_ALARM_SKIP_DAYS) |
                             repeat_skip[index];
    } else if(repeat_index(app->alarm_options) != REPEAT_CUSTOM) {
        // Custom starts from the weekdays, which its days page changes
        app->alarm_options = (app->alarm_options & ~FURI_HAL_RTC_ALARM_SKIP_DAYS) | 0x60;
    }
    alarm_options_save(app);
    refresh_days_item(app);
    // its days slide in only for Custom
    variable_item_set_hidden(app->alarm_days_item, index != REPEAT_CUSTOM);
}

static void snooze_changed(VariableItem* item) {
    AppState* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, snooze_text[index]);
    app->alarm_options = (app->alarm_options & ~(0x1FUL << FURI_HAL_RTC_ALARM_SNOOZE_SHIFT)) |
                         ((uint32_t)snooze_minutes[index] << FURI_HAL_RTC_ALARM_SNOOZE_SHIFT);
    alarm_options_save(app);
}

static void rings_changed(VariableItem* item) {
    AppState* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, rings_text[index]);
    app->alarm_options = (app->alarm_options & ~(0x1FUL << FURI_HAL_RTC_ALARM_RING_SHIFT)) |
                         ((uint32_t)rings_minutes[index] << FURI_HAL_RTC_ALARM_RING_SHIFT);
    alarm_options_save(app);
}

/** A switch whose option bit means "off" (no sound, no vibration...) or "on" (gentle) */
static void option_switch_changed(VariableItem* item, uint32_t bit, bool bit_means_on) {
    AppState* app = variable_item_get_context(item);
    const bool on = variable_item_get_current_value_index(item) == 1;
    variable_item_set_current_value_text(item, alarm_toggle_text[on]);
    if(on == bit_means_on) {
        app->alarm_options |= bit;
    } else {
        app->alarm_options &= ~bit;
    }
    alarm_options_save(app);
}

static void sound_changed(VariableItem* item) {
    option_switch_changed(item, FURI_HAL_RTC_ALARM_SILENT, false);
    AppState* app = variable_item_get_context(item);
    // a gentle start means nothing without sound: it slides away
    variable_item_set_hidden(
        variable_item_list_get(app->alarm_menu, AlarmRowGentle),
        app->alarm_options & FURI_HAL_RTC_ALARM_SILENT);
}

static void gentle_changed(VariableItem* item) {
    option_switch_changed(item, FURI_HAL_RTC_ALARM_GENTLE, true);
}

static void vibro_changed(VariableItem* item) {
    option_switch_changed(item, FURI_HAL_RTC_ALARM_NO_VIBRO, false);
}

static void light_changed(VariableItem* item) {
    option_switch_changed(item, FURI_HAL_RTC_ALARM_NO_LIGHT, false);
}

static VariableItem*
    add_switch(AppState* app, const char* label, VariableItemChangeCallback callback, bool on) {
    VariableItem* item = variable_item_list_add(app->alarm_menu, label, 2, callback, app);
    variable_item_set_values_text(item, alarm_toggle_text);
    variable_item_set_current_value_index(item, on);
    variable_item_set_current_value_text(item, alarm_toggle_text[on]);
    return item;
}

static VariableItem* add_choice(
    AppState* app,
    const char* label,
    const char* const* texts,
    uint8_t count,
    uint8_t index,
    VariableItemChangeCallback callback) {
    VariableItem* item = variable_item_list_add(app->alarm_menu, label, count, callback, app);
    variable_item_set_values_text(item, texts);
    variable_item_set_current_value_index(item, index);
    variable_item_set_current_value_text(item, texts[index]);
    return item;
}

/** The alarm page's rows, from the options the chip keeps */
static void alarm_menu_build(AppState* app) {
    const uint32_t options = app->alarm_options;
    app->alarm_toggle_item =
        add_switch(app, "Alarm", alarm_toggle_changed, app->settings.alarm_enabled);
    app->alarm_time_item = variable_item_list_add(app->alarm_menu, "Time", 1, NULL, app);
    variable_item_set_opens(app->alarm_time_item, true);
    const uint8_t repeat = repeat_index(options);
    add_choice(app, "Repeat", repeat_text, COUNT_OF(repeat_text), repeat, repeat_changed);
    app->alarm_days_item = variable_item_list_add(app->alarm_menu, "Days", 1, NULL, app);
    variable_item_set_opens(app->alarm_days_item, true);
    variable_item_set_indent(app->alarm_days_item, 1);
    variable_item_set_hidden(app->alarm_days_item, repeat != REPEAT_CUSTOM);
    refresh_days_item(app);
    add_choice(
        app,
        "Snooze",
        snooze_text,
        COUNT_OF(snooze_text),
        nearest_index(
            snooze_minutes, COUNT_OF(snooze_minutes), furi_hal_rtc_alarm_snooze_minutes(options)),
        snooze_changed);
    add_choice(
        app,
        "Rings for",
        rings_text,
        COUNT_OF(rings_text),
        nearest_index(
            rings_minutes, COUNT_OF(rings_minutes), furi_hal_rtc_alarm_ring_minutes(options)),
        rings_changed);
    const bool sound = !(options & FURI_HAL_RTC_ALARM_SILENT);
    add_switch(app, "Sound", sound_changed, sound);
    VariableItem* gentle =
        add_switch(app, "Gentle start", gentle_changed, options & FURI_HAL_RTC_ALARM_GENTLE);
    variable_item_set_indent(gentle, 1);
    variable_item_set_hidden(gentle, !sound);
    add_switch(app, "Vibration", vibro_changed, !(options & FURI_HAL_RTC_ALARM_NO_VIBRO));
    add_switch(app, "Light", light_changed, !(options & FURI_HAL_RTC_ALARM_NO_LIGHT));
}

// ---------------- the days a custom repeat rings on ----------------

static void alarm_days_draw(Canvas* canvas, void* ctx) {
    ClockModel* model = ctx;
    AppState* app = model->app;
    canvas_clear(canvas);
    elements_header(canvas, "Repeat");
    // seven boxes, the days it rings filled, the one under the cursor framed twice
    for(uint8_t day = 0; day < 7; day++) {
        const int32_t x = 4 + day * 17;
        const int32_t y = 22;
        const bool rings = !(app->alarm_options & (1UL << day));
        char letter[2] = {day_letters[day], '\0'};
        canvas_set_font(canvas, FontPrimary);
        if(rings) {
            canvas_draw_rbox(canvas, x, y, 15, 15, 3);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_draw_rframe(canvas, x, y, 15, 15, 3);
        }
        canvas_draw_str_aligned(canvas, x + 8, y + 8, AlignCenter, AlignCenter, letter);
        canvas_set_color(canvas, ColorBlack);
        if(day == app->days_cursor) {
            // a mark under the day the cursor is on
            canvas_draw_box(canvas, x + 4, y + 18, 7, 2);
        }
    }
    elements_hint_bar(canvas, "lr=Day;ok=On/off;back=Done");
}

static bool alarm_days_input(InputEvent* event, void* context) {
    AppState* app = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;
    if(event->key == InputKeyLeft) {
        app->days_cursor = (app->days_cursor + 6) % 7;
    } else if(event->key == InputKeyRight) {
        app->days_cursor = (app->days_cursor + 1) % 7;
    } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
        app->alarm_options ^= 1UL << app->days_cursor;
        // never no day at all: that would be Once, which the Repeat row says
        if((app->alarm_options & FURI_HAL_RTC_ALARM_SKIP_DAYS) == FURI_HAL_RTC_ALARM_SKIP_DAYS) {
            app->alarm_options ^= 1UL << app->days_cursor;
        }
        alarm_options_save(app);
    } else {
        return false;
    }
    with_view_model(app->alarm_days_view, ClockModel * m, { UNUSED(m); }, true);
    return true;
}

static void refresh_time_menu_item(AppState* app) {
    // Keep the "Set time" row's value text in step with the chosen alarm time.
    char buf[12];
    format_alarm_time(
        buf, sizeof(buf), app->settings.alarm_hour, app->settings.alarm_minute, app->time_format);
    variable_item_set_current_value_text(app->alarm_time_item, buf);
}

static void alarm_menu_enter(void* context, uint32_t index) {
    AppState* app = context;
    if(index == AlarmRowDays) {
        ns_switch(app, NsViewAlarmDays);
        return;
    }
    if(index != AlarmRowTime) return; // the others are switches and choices

    app->edit_hour = app->settings.alarm_hour;
    app->edit_minute = app->settings.alarm_minute;
    app->edit_field = 0;
    ns_switch(app, NsViewAlarmTime);
}

// ---------------- alarm time picker (custom, time only) ----------------
// A plain HH:MM picker instead of the firmware date_time_input, which always
// shows the date too. The alarm has no date - it fires every day at this time.

static void alarm_time_draw(Canvas* canvas, void* ctx) {
    ClockModel* model = ctx;
    AppState* app = model->app;
    bool h12mode = (app->time_format == LocaleTimeFormat12h);
    canvas_clear(canvas);

    elements_header(canvas, "Alarm");

    char hh[4], mm[4];
    bool pm = false;
    if(h12mode) {
        uint8_t h12;
        to_12h(app->edit_hour, &h12, &pm);
        snprintf(hh, sizeof(hh), "%.2u", h12);
    } else {
        snprintf(hh, sizeof(hh), "%.2u", app->edit_hour);
    }
    snprintf(mm, sizeof(mm), "%.2u", app->edit_minute);

    // 24h: HH:MM centred. 12h: shift left to make room for AM/PM on the right.
    uint8_t hx = h12mode ? 34 : 45;
    uint8_t cx = h12mode ? 53 : 64;
    uint8_t mx = h12mode ? 72 : 83;

    const uint8_t mid = 34; // between the header and the hint bar
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, hx, mid, AlignCenter, AlignCenter, hh);
    canvas_draw_str_aligned(canvas, cx, mid, AlignCenter, AlignCenter, ":");
    canvas_draw_str_aligned(canvas, mx, mid, AlignCenter, AlignCenter, mm);
    if(h12mode) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 104, mid, AlignCenter, AlignCenter, pm ? "PM" : "AM");
    }

    // The field Up and Down turn, as a picker's wheel: a chevron over it and one under it
    const uint8_t ux = app->edit_field == 0 ? hx : app->edit_field == 1 ? mx : 104;
    for(uint8_t row = 0; row < 3; row++) {
        canvas_draw_line(canvas, ux - row, mid - 14 + row, ux + row, mid - 14 + row);
        canvas_draw_line(canvas, ux - row, mid + 14 - row, ux + row, mid + 14 - row);
    }

    elements_hint_bar(canvas, "ud=Set;lr=Move;ok=Save");
}

static bool alarm_time_input(InputEvent* event, void* context) {
    AppState* app = context;
    bool h12mode = (app->time_format == LocaleTimeFormat12h);
    uint8_t max_field = h12mode ? 2 : 1; // extra meridian field in 12h mode

    bool value_evt = (event->type == InputTypeShort || event->type == InputTypeRepeat);
    bool nav_evt = (event->type == InputTypeShort);

    switch(event->key) {
    case InputKeyUp:
    case InputKeyDown: {
        if(!value_evt) return false; // auto-repeat only for value changes
        bool up = (event->key == InputKeyUp);
        if(app->edit_field == 0) {
            if(h12mode) {
                uint8_t h12;
                bool pm;
                to_12h(app->edit_hour, &h12, &pm);
                h12 = up ? ((h12 % 12) + 1) : ((h12 == 1) ? 12 : h12 - 1);
                app->edit_hour = to_24h(h12, pm);
            } else {
                app->edit_hour = (app->edit_hour + (up ? 1 : 23)) % 24;
            }
        } else if(app->edit_field == 1) {
            app->edit_minute = (app->edit_minute + (up ? 1 : 59)) % 60;
        } else { // meridian: either key flips AM/PM
            uint8_t h12;
            bool pm;
            to_12h(app->edit_hour, &h12, &pm);
            app->edit_hour = to_24h(h12, !pm);
        }
        break;
    }
    case InputKeyLeft:
        if(!nav_evt) return false;
        if(app->edit_field > 0) app->edit_field--;
        break;
    case InputKeyRight:
        if(!nav_evt) return false;
        if(app->edit_field < max_field) app->edit_field++;
        break;
    case InputKeyOk:
    case InputKeyBack:
        if(!nav_evt) return true; // swallow held repeats, act on the click
        // Both confirm - there's no separate cancel, so leaving keeps the value.
        app->settings.alarm_hour = app->edit_hour;
        app->settings.alarm_minute = app->edit_minute;
        ns_alarm_write(app);
        refresh_time_menu_item(app);
        ns_switch(app, NsViewAlarmMenu);
        alarm_set_show(app);
        return true;
    default:
        return false;
    }
    with_view_model(app->alarm_time_view, ClockModel * m, { UNUSED(m); }, true);
    return true;
}

static void alarm_set_popup_done(void* context) {
    AppState* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, NsCustomAlarmSetDone);
}

// ---------------- periodic tick ----------------

static void ns_tick(AppState* app) {
    app->battery_pct = furi_hal_power_get_pct();

    // Only the clock view is model-driven; the menu/time views repaint
    // themselves. Committing the (unchanged) model forces a redraw.
    if(app->current_view == NsViewClock) {
        with_view_model(app->clock_view, ClockModel * m, { UNUSED(m); }, true);
    }
}

static void ns_timer_callback(void* context) {
    AppState* app = context;
    // Runs on the timer task: just poke the dispatcher, do the work on its thread.
    view_dispatcher_send_custom_event(app->view_dispatcher, NsCustomTick);
}

static bool ns_custom_event_callback(void* context, uint32_t event) {
    AppState* app = context;
    if(event == NsCustomTick) {
        ns_tick(app);
        return true;
    }
    if(event == NsCustomAlarmSetDone) {
        if(app->current_view == NsViewAlarmSet) ns_switch(app, NsViewAlarmMenu);
        return true;
    }
    return false;
}

static bool ns_navigation_callback(void* context) {
    AppState* app = context;
    switch(app->current_view) {
    case NsViewAlarmMenu:
        ns_switch(app, NsViewClock);
        return true;
    case NsViewAlarmTime:
    case NsViewAlarmDays:
    case NsViewAlarmSet:
        if(app->current_view == NsViewAlarmDays) refresh_days_item(app);
        ns_switch(app, NsViewAlarmMenu);
        return true;
    case NsViewClock:
    default:
        // Back from the clock exits: returning false stops the dispatcher.
        return false;
    }
}

int32_t clock_app(void* p) {
    UNUSED(p);
    AppState* app = malloc(sizeof(AppState));
    memset(app, 0, sizeof(AppState));

    app->time_format = locale_get_time_format();
    app->date_format = locale_get_date_format();
    app->battery_pct = furi_hal_power_get_pct();
    app->current_view = NsViewClock;

    // Make sure this fap's data folder exists before we load/save from it.
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, NS_SETTINGS_DIR);
    furi_record_close(RECORD_STORAGE);

    // Save current user settings, disable backlight delay, force display always on.
    notif = furi_record_open(RECORD_NOTIFICATION);
    float SavedBrightness = notif->settings.display_brightness;
    uint32_t Saved_display_off_delay_ms = notif->settings.display_off_delay_ms;

    // Load persisted alarm + brightness. If absent, keep the alarm off and fall
    // back to the system brightness so first launch behaves like before.
    if(saved_struct_load(
           NS_SETTINGS_PATH,
           &app->settings,
           sizeof(NsSettings),
           NS_SETTINGS_MAGIC,
           NS_SETTINGS_VERSION)) {
        brightness = app->settings.brightness;
        if(brightness > 100) brightness = 100;
    } else {
        app->settings.alarm_enabled = false;
        brightness = (int)roundf(SavedBrightness * 100.f);
        app->settings.brightness = (uint8_t)brightness;
    }
    // This app kept an alarm of its own once, that rang only while it was open: one still on
    // moves to the clock chip's (unless that one is already set), and the file keeps it no more
    if(app->settings.alarm_enabled) {
        DateTime chip;
        if(!furi_hal_rtc_get_alarm(&chip)) ns_alarm_write(app);
        app->settings.alarm_enabled = false;
        ns_settings_save(app);
    }
    ns_alarm_read(app);

    notif->settings.display_off_delay_ms = 0;
    notification_message(notif, &sequence_display_backlight_enforce_on);
    // Apply the restored brightness now (enforce_on above only locks in whatever
    // the system was already at).
    set_enforced_brightness((float)(brightness / 100.f));
    notification_message(notif, &led_off);

    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, ns_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, ns_navigation_callback);

    // Clock view (custom)
    app->clock_view = view_alloc();
    view_allocate_model(app->clock_view, ViewModelTypeLocking, sizeof(ClockModel));
    with_view_model(app->clock_view, ClockModel * m, { m->app = app; }, false);
    view_set_context(app->clock_view, app);
    view_set_draw_callback(app->clock_view, clock_render_callback);
    view_set_input_callback(app->clock_view, clock_input_callback);
    view_dispatcher_add_view(app->view_dispatcher, NsViewClock, app->clock_view);

    // Alarm menu
    app->alarm_menu = variable_item_list_alloc();
    variable_item_list_set_header(app->alarm_menu, "Alarm");
    app->alarm_options = furi_hal_rtc_get_register(FuriHalRtcRegisterAlarm);
    alarm_menu_build(app);
    refresh_time_menu_item(app);
    variable_item_list_set_enter_callback(app->alarm_menu, alarm_menu_enter, app);
    view_dispatcher_add_view(
        app->view_dispatcher, NsViewAlarmMenu, variable_item_list_get_view(app->alarm_menu));

    // Alarm time picker (custom, HH:MM only)
    app->alarm_time_view = view_alloc();
    view_allocate_model(app->alarm_time_view, ViewModelTypeLocking, sizeof(ClockModel));
    with_view_model(app->alarm_time_view, ClockModel * m, { m->app = app; }, false);
    view_set_context(app->alarm_time_view, app);
    view_set_draw_callback(app->alarm_time_view, alarm_time_draw);
    view_set_input_callback(app->alarm_time_view, alarm_time_input);
    view_dispatcher_add_view(app->view_dispatcher, NsViewAlarmTime, app->alarm_time_view);

    // The days of a custom repeat
    app->alarm_days_view = view_alloc();
    view_allocate_model(app->alarm_days_view, ViewModelTypeLocking, sizeof(ClockModel));
    with_view_model(app->alarm_days_view, ClockModel * m, { m->app = app; }, false);
    view_set_context(app->alarm_days_view, app);
    view_set_draw_callback(app->alarm_days_view, alarm_days_draw);
    view_set_input_callback(app->alarm_days_view, alarm_days_input);
    view_dispatcher_add_view(app->view_dispatcher, NsViewAlarmDays, app->alarm_days_view);

    // "Alarm set, rings in 7 h 12 min", a moment after it is set
    app->alarm_set_popup = popup_alloc();
    popup_set_header(app->alarm_set_popup, "Alarm set", 64, 20, AlignCenter, AlignCenter);
    popup_set_timeout(app->alarm_set_popup, 1500);
    popup_enable_timeout(app->alarm_set_popup);
    popup_set_context(app->alarm_set_popup, app);
    popup_set_callback(app->alarm_set_popup, alarm_set_popup_done);
    view_dispatcher_add_view(
        app->view_dispatcher, NsViewAlarmSet, popup_get_view(app->alarm_set_popup));

    // Twice a second, so the seconds shown never skip one (the alarm rings by itself)
    app->timer = furi_timer_alloc(ns_timer_callback, FuriTimerTypePeriodic, app);
    furi_timer_start(app->timer, furi_kernel_get_tick_frequency() / 2);

    ns_switch(app, NsViewClock);
    view_dispatcher_run(app->view_dispatcher);

    // ---- teardown ----
    // Persist the final brightness (alarm changes already saved themselves).
    ns_settings_save(app);

    furi_timer_stop(app->timer);
    furi_timer_free(app->timer);

    view_dispatcher_remove_view(app->view_dispatcher, NsViewClock);
    view_dispatcher_remove_view(app->view_dispatcher, NsViewAlarmMenu);
    view_dispatcher_remove_view(app->view_dispatcher, NsViewAlarmTime);
    view_dispatcher_remove_view(app->view_dispatcher, NsViewAlarmDays);
    view_dispatcher_remove_view(app->view_dispatcher, NsViewAlarmSet);
    view_free(app->alarm_days_view);
    popup_free(app->alarm_set_popup);
    view_free(app->clock_view);
    variable_item_list_free(app->alarm_menu);
    view_free(app->alarm_time_view);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);

    // Restore display backlight settings.
    notif->settings.display_off_delay_ms = Saved_display_off_delay_ms;
    notification_message(notif, &sequence_display_backlight_enforce_auto);
    set_backlight_brightness(SavedBrightness);
    notification_message(notif, &led_reset);
    furi_record_close(RECORD_NOTIFICATION);

    free(app);
    return 0;
}
