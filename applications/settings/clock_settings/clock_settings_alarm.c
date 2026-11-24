#include <furi.h>
#include <gui/elements.h>
#include <furi_hal.h>

#include <gui/gui.h>
#include <gui/view_port.h>

#include <notification/notification.h>
#include <notification/notification_messages.h>

#include <assets_icons.h>
#include <locale/locale.h>

#define TAG "ClockSettingsAlarm"

/* How it rings, from the alarm's options (furi_hal_rtc.h): each 2 s, two beats of sound,
 * vibration and the screen flashing, whichever are on. Rebuilt each time for a gentle start's
 * growing volume, in one of two buffers: the other may still be playing */
#define ALARM_SEQUENCE_MAX 24

typedef struct {
    NotificationMessage volume;
    const NotificationMessage* sequence[ALARM_SEQUENCE_MAX];
} AlarmRing;

typedef struct {
    DateTime now;
    DateTime snooze_until;
    DateTime alarm_start;
    IconAnimation* icon;

    bool is_snooze;
    uint32_t options;
    uint32_t ring_start; // when it began ringing (again, after a snooze): it gives up after a while
    uint32_t cycle; // 2 s rings so far, for the gentle start
    AlarmRing ring[2];
} ClockSettingsAlramModel;

static const NotificationSequence* alarm_ring_build(ClockSettingsAlramModel* model) {
    AlarmRing* ring = &model->ring[model->cycle % 2];
    const uint32_t options = model->options;
    const bool sound = !(options & FURI_HAL_RTC_ALARM_SILENT);
    const bool vibro = !(options & FURI_HAL_RTC_ALARM_NO_VIBRO);
    const bool light = !(options & FURI_HAL_RTC_ALARM_NO_LIGHT);
    // gentle: a tenth of the volume, a tenth more each ring, full after about 20 s
    const float volume = (options & FURI_HAL_RTC_ALARM_GENTLE) ?
                             MIN(1.0f, 0.1f + 0.1f * (float)model->cycle) :
                             1.0f;
    ring->volume = (NotificationMessage){
        .type = NotificationMessageTypeForceSpeakerVolumeSetting,
        .data.forced_settings.speaker_volume = volume,
    };
    size_t n = 0;
    if(sound) ring->sequence[n++] = &ring->volume;
    if(vibro) {
        ring->sequence[n++] = &message_force_vibro_setting_on;
        ring->sequence[n++] = &message_vibro_on;
    }
    if(light) ring->sequence[n++] = &message_force_display_brightness_setting_1f;
    for(uint8_t beat = 0; beat < 2; beat++) {
        if(light) ring->sequence[n++] = &message_display_backlight_on;
        if(sound) ring->sequence[n++] = &message_note_c7;
        ring->sequence[n++] = &message_delay_250;
        if(light) ring->sequence[n++] = &message_display_backlight_off;
        if(sound) ring->sequence[n++] = &message_note_c4;
        ring->sequence[n++] = &message_delay_250;
    }
    if(sound) ring->sequence[n++] = &message_sound_off;
    if(vibro) ring->sequence[n++] = &message_vibro_off;
    ring->sequence[n] = NULL;
    model->cycle++;
    return (const NotificationSequence*)ring->sequence;
}

static void clock_settings_alarm_draw_callback(Canvas* canvas, void* ctx) {
    ClockSettingsAlramModel* model = ctx;
    char buffer[64] = {};

    // Clock icon
    canvas_draw_icon_animation(canvas, 5, 6, model->icon);

    // Time and date in the system's formats, as the Clock app and the desktop show them
    const LocaleTimeFormat time_format = locale_get_time_format();
    uint8_t hour = model->now.hour;
    if(time_format == LocaleTimeFormat12h) {
        hour %= 12;
        if(!hour) hour = 12;
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, 126, 2, AlignRight, AlignTop, model->now.hour >= 12 ? "PM" : "AM");
    }
    canvas_set_font(canvas, FontBigNumbers);
    snprintf(buffer, sizeof(buffer), "%02u:%02u", hour, model->now.minute);
    canvas_draw_str(canvas, 58, 32, buffer);

    canvas_set_font(canvas, FontPrimary);
    FuriString* date = furi_string_alloc();
    // as the Clock app: year first with dashes (ISO), else with dots
    const LocaleDateFormat date_format = locale_get_date_format();
    locale_format_date(
        date, &model->now, date_format, date_format == LocaleDateFormatYMD ? "-" : ".");
    canvas_draw_str(canvas, 60, 44, furi_string_get_cstr(date));
    furi_string_free(date);

    // both its keys, as every screen says them: Back snoozes, any other key stops it
    char hints[40];
    snprintf(
        hints,
        sizeof(hints),
        "ok=Stop;back=Snooze %u min",
        furi_hal_rtc_alarm_snooze_minutes(model->options));
    elements_hint_bar(canvas, hints);
}

static void clock_settings_alarm_input_callback(InputEvent* input_event, void* ctx) {
    furi_assert(ctx);
    FuriMessageQueue* event_queue = ctx;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

void clock_settings_alarm_animation_callback(IconAnimation* instance, void* context) {
    UNUSED(instance);
    ViewPort* view_port = context;
    view_port_update(view_port);
}

int32_t clock_settings_alarm(void* p) {
    UNUSED(p);

    // View Model
    // on the heap: the ring's two sequences would crowd the thread's 1 KB stack
    ClockSettingsAlramModel* model = malloc(sizeof(ClockSettingsAlramModel));
    memset(model, 0, sizeof(ClockSettingsAlramModel));
    model->is_snooze = false;
    model->options = furi_hal_rtc_get_register(FuriHalRtcRegisterAlarm);

    furi_hal_rtc_get_datetime(&model->now);
    model->ring_start = datetime_datetime_to_timestamp(&model->now);
    furi_hal_rtc_get_alarm(&model->alarm_start);
    model->icon = icon_animation_alloc(&A_Alarm_47x39);

    // Alloc message queue
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    // Configure view port
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, clock_settings_alarm_draw_callback, model);
    view_port_input_callback_set(view_port, clock_settings_alarm_input_callback, event_queue);

    // Register view port in GUI
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_set_lockdown_inhibit(gui, true);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    notification_message(notification, alarm_ring_build(model));

    icon_animation_set_update_callback(
        model->icon, clock_settings_alarm_animation_callback, view_port);
    icon_animation_start(model->icon);

    // Process events
    InputEvent event;
    bool running = true;
    // A key acts as it is let go, not as it goes down: the screen stays until the whole press has
    // come and gone, so no part of it (its short press, its release) reaches the desktop under
    // it. Acting on the press, OK that stopped the alarm also opened the main menu
    InputKey pressed = InputKeyMAX;
    while(running) {
        if(furi_message_queue_get(event_queue, &event, 2000) == FuriStatusOk) {
            if(event.type == InputTypePress) {
                pressed = event.key;
            } else if(event.type == InputTypeRelease && event.key == pressed) {
                pressed = InputKeyMAX;
                // Snooze
                if(event.key == InputKeyBack) {
                    furi_hal_rtc_get_datetime(&model->snooze_until);
                    model->snooze_until.minute +=
                        furi_hal_rtc_alarm_snooze_minutes(model->options);
                    model->snooze_until.hour += model->snooze_until.minute / 60;
                    model->snooze_until.minute %= 60;
                    model->snooze_until.hour %= 24;

                    model->is_snooze = true;
                    model->alarm_start = model->snooze_until; // For correct timeout behavior
                    view_port_enabled_set(view_port, false);
                    gui_set_lockdown_inhibit(gui, false);
                } else {
                    running = false;
                }
            }
        } else if(model->is_snooze) {
            furi_hal_rtc_get_datetime(&model->now);
            if(datetime_datetime_to_timestamp(&model->now) >=
               datetime_datetime_to_timestamp(&model->snooze_until)) {
                view_port_enabled_set(view_port, true);
                gui_set_lockdown_inhibit(gui, true);

                model->is_snooze = false;
                // rings for its time again, gently again
                model->ring_start = datetime_datetime_to_timestamp(&model->now);
                model->cycle = 0;
            }
        } else {
            notification_message(notification, alarm_ring_build(model));
            furi_hal_rtc_get_datetime(&model->now);
            view_port_update(view_port);

            // it gives up after its minutes of ringing (from the time, not the clock's hour)
            if(datetime_datetime_to_timestamp(&model->now) - model->ring_start >=
               furi_hal_rtc_alarm_ring_minutes(model->options) * 60U) {
                running = false;
            }
        }
    }

    icon_animation_stop(model->icon);

    notification_message_block(notification, &sequence_empty);
    furi_record_close(RECORD_NOTIFICATION);

    view_port_enabled_set(view_port, false);
    gui_set_lockdown_inhibit(gui, false);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    furi_record_close(RECORD_GUI);

    icon_animation_free(model->icon);

    // a one-time alarm is done: it turns itself off
    if(furi_hal_rtc_alarm_once(model->options)) furi_hal_rtc_set_alarm(NULL, false);
    free(model);

    return 0;
}

FuriThread* clock_settings_alarm_thread = NULL;

static void clock_settings_alarm_thread_state_callback(
    FuriThread* thread,
    FuriThreadState state,
    void* context) {
    furi_assert(clock_settings_alarm_thread == thread);
    UNUSED(context);

    if(state == FuriThreadStateStopped) {
        furi_thread_free(thread);
        clock_settings_alarm_thread = NULL;
    }
}

static void clock_settings_alarm_start(void* context, uint32_t arg) {
    UNUSED(context);
    UNUSED(arg);

    if(clock_settings_alarm_thread) return;

    // not a day it rings on: the chip's alarm comes every day, the days are the options'
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    if(!furi_hal_rtc_alarm_rings_on(
           furi_hal_rtc_get_register(FuriHalRtcRegisterAlarm), now.weekday)) {
        FURI_LOG_I(TAG, "not today");
        return;
    }
    FURI_LOG_I(TAG, "spawning alarm thread");

    clock_settings_alarm_thread =
        furi_thread_alloc_ex("ClockAlarm", 1024, clock_settings_alarm, NULL);
    furi_thread_set_state_callback(
        clock_settings_alarm_thread, clock_settings_alarm_thread_state_callback);
    furi_thread_start(clock_settings_alarm_thread);
}

static void clock_settings_alarm_isr(void* context) {
    UNUSED(context);
    furi_timer_pending_callback(clock_settings_alarm_start, NULL, 0);
}

void clock_settings_start(void) {
#ifndef FURI_RAM_EXEC
    furi_hal_rtc_set_alarm_callback(clock_settings_alarm_isr, NULL);
#endif
}
