#include "clock_settings_module.h"

#include <gui/elements.h>
#include <assets_icons.h>
#include <locale/locale.h>

#define TAG "ClockSettingsModule"

struct ClockSettingsModule {
    FuriEventLoopTimer* timer;
    View* view;
};

typedef struct {
    DateTime current;
    DateTime alarm;
    bool alarm_enabled;
    bool editing;

    uint8_t row;
    uint8_t column;
} ClockSettingsModuleViewModel;

typedef enum {
    EditStateNone,
    EditStateActive,
    EditStateActiveEditing,
} EditState;

#define get_state(m, r, c)                                           \
    ((m)->row == (r) && (m)->column == (c) ?                         \
         ((m)->editing ? EditStateActiveEditing : EditStateActive) : \
         EditStateNone)

/* Under the header every Settings page has: three rows of the same height, a gap between them for
 * the arrows a field shows while it is changed */
#define ROW_0_Y (19)
#define ROW_0_H (11)

#define ROW_1_Y (35)
#define ROW_1_H (11)

#define ROW_2_Y (51)
#define ROW_2_H (11)

#define ROW_COUNT    3
#define COLUMN_COUNT 3

static inline void clock_settings_module_cleanup_date(DateTime* dt) {
    uint8_t day_per_month =
        datetime_get_days_per_month(datetime_is_leap_year(dt->year), dt->month);
    if(dt->day > day_per_month) {
        dt->day = day_per_month;
    }
}

static inline void clock_settings_module_draw_block(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    size_t w,
    size_t h,
    Font font,
    EditState state,
    const char* text) {
    canvas_set_color(canvas, ColorBlack);
    if(state != EditStateNone) {
        if(state == EditStateActiveEditing) {
            // in the gaps between the rows, clear of the neighbouring fields
            canvas_draw_icon(canvas, x + w / 2 - 2, y - 5, &I_SmallArrowUp_3x5);
            canvas_draw_icon(canvas, x + w / 2 - 2, y + h, &I_SmallArrowDown_3x5);
        }
        canvas_draw_rbox(canvas, x, y, w, h, 1);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_rframe(canvas, x, y, w, h, 1);
    }

    canvas_set_font(canvas, font);
    canvas_draw_str_aligned(canvas, x + w / 2, y + h / 2, AlignCenter, AlignCenter, text);
    if(state != EditStateNone) {
        canvas_set_color(canvas, ColorBlack);
    }
}

static void
    clock_settings_module_draw_time_callback(Canvas* canvas, ClockSettingsModuleViewModel* model) {
    char buffer[64];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, ROW_0_Y + 9, "Time");

    // as the date under it: three fields, a colon between them
    snprintf(buffer, sizeof(buffer), "%02u", model->current.hour);
    clock_settings_module_draw_block(
        canvas, 44, ROW_0_Y, 17, ROW_0_H, FontPrimary, get_state(model, 0, 0), buffer);
    canvas_draw_box(canvas, 65, ROW_0_Y + ROW_0_H - 4, 2, 2);
    canvas_draw_box(canvas, 65, ROW_0_Y + ROW_0_H - 8, 2, 2);

    snprintf(buffer, sizeof(buffer), "%02u", model->current.minute);
    clock_settings_module_draw_block(
        canvas, 71, ROW_0_Y, 17, ROW_0_H, FontPrimary, get_state(model, 0, 1), buffer);
    canvas_draw_box(canvas, 92, ROW_0_Y + ROW_0_H - 4, 2, 2);
    canvas_draw_box(canvas, 92, ROW_0_Y + ROW_0_H - 8, 2, 2);

    snprintf(buffer, sizeof(buffer), "%02u", model->current.second);
    clock_settings_module_draw_block(
        canvas, 98, ROW_0_Y, 17, ROW_0_H, FontPrimary, get_state(model, 0, 2), buffer);
}

static void
    clock_settings_module_draw_date_callback(Canvas* canvas, ClockSettingsModuleViewModel* model) {
    char buffer[64];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, ROW_1_Y + 9, "Date");
    // Day
    snprintf(buffer, sizeof(buffer), "%02u", model->current.day);
    clock_settings_module_draw_block(
        canvas, 44, ROW_1_Y, 17, ROW_1_H, FontPrimary, get_state(model, 1, 0), buffer);
    canvas_draw_box(canvas, 71 - 6, ROW_1_Y + ROW_1_H - 4, 2, 2);
    // Month
    snprintf(buffer, sizeof(buffer), "%02u", model->current.month);
    clock_settings_module_draw_block(
        canvas, 71, ROW_1_Y, 17, ROW_1_H, FontPrimary, get_state(model, 1, 1), buffer);
    canvas_draw_box(canvas, 98 - 6, ROW_1_Y + ROW_1_H - 4, 2, 2);
    // Year
    snprintf(buffer, sizeof(buffer), "%04u", model->current.year);
    clock_settings_module_draw_block(
        canvas, 98, ROW_1_Y, 30, ROW_1_H, FontPrimary, get_state(model, 1, 2), buffer);
}

static void
    clock_settings_module_draw_alarm_callback(Canvas* canvas, ClockSettingsModuleViewModel* model) {
    char buffer[64];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, ROW_2_Y + 9, "Alarm");

    // in the system's time format, as the Clock app shows the same alarm: 12h with AM/PM
    const bool h12 = locale_get_time_format() == LocaleTimeFormat12h;
    const int32_t hour_x = h12 ? 40 : 58;
    uint8_t hour = model->alarm.hour;
    if(h12) {
        hour %= 12;
        if(!hour) hour = 12;
    }
    snprintf(buffer, sizeof(buffer), "%02u", hour);
    clock_settings_module_draw_block(
        canvas, hour_x, ROW_2_Y, 17, ROW_2_H, FontPrimary, get_state(model, 2, 0), buffer);
    canvas_draw_box(canvas, hour_x + 19, ROW_2_Y + ROW_2_H - 4, 2, 2);
    canvas_draw_box(canvas, hour_x + 19, ROW_2_Y + ROW_2_H - 4 - 4, 2, 2);

    snprintf(buffer, sizeof(buffer), "%02u", model->alarm.minute);
    clock_settings_module_draw_block(
        canvas, hour_x + 23, ROW_2_Y, 17, ROW_2_H, FontPrimary, get_state(model, 2, 1), buffer);
    if(h12) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, hour_x + 43, ROW_2_Y + 9, model->alarm.hour >= 12 ? "PM" : "AM");
    }

    clock_settings_module_draw_block(
        canvas,
        106,
        ROW_2_Y,
        22,
        ROW_2_H,
        FontPrimary,
        get_state(model, 2, 2),
        model->alarm_enabled ? "On" : "Off");
}

static void clock_settings_module_draw_callback(Canvas* canvas, void* _model) {
    ClockSettingsModuleViewModel* model = _model;
    elements_header(canvas, "Clock & alarm");
    clock_settings_module_draw_time_callback(canvas, model);
    clock_settings_module_draw_date_callback(canvas, model);
    clock_settings_module_draw_alarm_callback(canvas, model);
}

static bool clock_settings_module_input_navigation_callback(
    InputEvent* event,
    ClockSettingsModuleViewModel* model) {
    if(event->key == InputKeyUp) {
        if(model->row > 0) model->row--;
    } else if(event->key == InputKeyDown) {
        if(model->row < ROW_COUNT - 1) model->row++;
    } else if(event->key == InputKeyOk) {
        model->editing = !model->editing;
    } else if(event->key == InputKeyRight) {
        if(model->column < COLUMN_COUNT - 1) model->column++;
    } else if(event->key == InputKeyLeft) {
        if(model->column > 0) model->column--;
    } else if(event->key == InputKeyBack && model->editing) {
        model->editing = false;
    } else {
        return false;
    }

    return true;
}

static bool clock_settings_module_input_time_callback(
    InputEvent* event,
    ClockSettingsModuleViewModel* model) {
    if(event->key == InputKeyUp) {
        if(model->column == 0) {
            model->current.hour++;
            model->current.hour = model->current.hour % 24;
        } else if(model->column == 1) {
            model->current.minute++;
            model->current.minute = model->current.minute % 60;
        } else if(model->column == 2) {
            model->current.second++;
            model->current.second = model->current.second % 60;
        } else {
            furi_crash();
        }
    } else if(event->key == InputKeyDown) {
        if(model->column == 0) {
            if(model->current.hour > 0) {
                model->current.hour--;
            } else {
                model->current.hour = 23;
            }
            model->current.hour = model->current.hour % 24;
        } else if(model->column == 1) {
            if(model->current.minute > 0) {
                model->current.minute--;
            } else {
                model->current.minute = 59;
            }
            model->current.minute = model->current.minute % 60;
        } else if(model->column == 2) {
            if(model->current.second > 0) {
                model->current.second--;
            } else {
                model->current.second = 59;
            }
            model->current.second = model->current.second % 60;
        } else {
            furi_crash();
        }
    } else {
        return clock_settings_module_input_navigation_callback(event, model);
    }

    return true;
}

static bool clock_settings_module_input_date_callback(
    InputEvent* event,
    ClockSettingsModuleViewModel* model) {
    if(event->key == InputKeyUp) {
        if(model->column == 0) {
            if(model->current.day < 31) model->current.day++;
        } else if(model->column == 1) {
            if(model->current.month < 12) {
                model->current.month++;
            }
        } else if(model->column == 2) {
            if(model->current.year < 2099) {
                model->current.year++;
            }
        } else {
            furi_crash();
        }
    } else if(event->key == InputKeyDown) {
        if(model->column == 0) {
            if(model->current.day > 1) {
                model->current.day--;
            }
        } else if(model->column == 1) {
            if(model->current.month > 1) {
                model->current.month--;
            }
        } else if(model->column == 2) {
            if(model->current.year > 2000) {
                model->current.year--;
            }
        } else {
            furi_crash();
        }
    } else {
        return clock_settings_module_input_navigation_callback(event, model);
    }

    clock_settings_module_cleanup_date(&model->current);

    return true;
}

static bool clock_settings_module_input_alarm_callback(
    InputEvent* event,
    ClockSettingsModuleViewModel* model) {
    if(event->key == InputKeyUp) {
        if(model->column == 0) {
            model->alarm.hour++;
            model->alarm.hour = model->alarm.hour % 24;
        } else if(model->column == 1) {
            model->alarm.minute++;
            model->alarm.minute = model->alarm.minute % 60;
        } else if(model->column == 2) {
            model->alarm_enabled = !model->alarm_enabled;
        } else {
            furi_crash();
        }
    } else if(event->key == InputKeyDown) {
        if(model->column == 0) {
            if(model->alarm.hour > 0) {
                model->alarm.hour--;
            } else {
                model->alarm.hour = 23;
            }
            model->alarm.hour = model->alarm.hour % 24;
        } else if(model->column == 1) {
            if(model->alarm.minute > 0) {
                model->alarm.minute--;
            } else {
                model->alarm.minute = 59;
            }
            model->alarm.minute = model->alarm.minute % 60;
        } else if(model->column == 2) {
            model->alarm_enabled = !model->alarm_enabled;
        } else {
            furi_crash();
        }
    } else {
        return clock_settings_module_input_navigation_callback(event, model);
    }

    return true;
}

static bool clock_settings_module_input_callback(InputEvent* event, void* context) {
    furi_assert(context);

    ClockSettingsModule* instance = context;
    bool consumed = false;

    with_view_model(
        instance->view,
        ClockSettingsModuleViewModel * model,
        {
            if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
                bool previous_editing = model->editing;
                if(model->editing) {
                    if(model->row == 0) {
                        consumed = clock_settings_module_input_time_callback(event, model);
                    } else if(model->row == 1) {
                        consumed = clock_settings_module_input_date_callback(event, model);
                    } else if(model->row == 2) {
                        consumed = clock_settings_module_input_alarm_callback(event, model);
                    } else {
                        furi_crash();
                    }
                } else {
                    consumed = clock_settings_module_input_navigation_callback(event, model);
                }

                // Switching between navigate/edit
                if(model->editing != previous_editing) {
                    if(model->row == 2) {
                        if(!model->editing) {
                            // Disable alarm
                            furi_hal_rtc_set_alarm(NULL, false);
                            // Set new alarm
                            furi_hal_rtc_set_alarm(&model->alarm, model->alarm_enabled);
                            // Confirm
                            model->alarm_enabled = furi_hal_rtc_get_alarm(&model->alarm);
                        }
                    } else {
                        if(model->editing) {
                            // stop timer to prevent mess with current date time
                            furi_event_loop_timer_stop(instance->timer);
                        } else {
                            // save date time and restart timer
                            furi_hal_rtc_set_datetime(&model->current);
                            furi_event_loop_timer_start(instance->timer, 1000);
                        }
                    }
                }
            }
        },
        true);

    return consumed;
}

static void clock_settings_module_timer_callback(void* context) {
    furi_assert(context);
    ClockSettingsModule* instance = context;

    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    with_view_model(
        instance->view, ClockSettingsModuleViewModel * model, { model->current = dt; }, true);
}

static void clock_settings_module_view_enter_callback(void* context) {
    furi_assert(context);
    ClockSettingsModule* instance = context;

    clock_settings_module_timer_callback(context);

    DateTime alarm;
    bool enabled = furi_hal_rtc_get_alarm(&alarm);

    with_view_model(
        instance->view,
        ClockSettingsModuleViewModel * model,
        {
            model->alarm = alarm;
            model->alarm_enabled = enabled;
        },
        true);

    furi_event_loop_timer_start(instance->timer, 1000);
}

static void clock_settings_module_view_exit_callback(void* context) {
    furi_assert(context);
    ClockSettingsModule* instance = context;
    furi_event_loop_timer_stop(instance->timer);
}

ClockSettingsModule* clock_settings_module_alloc(FuriEventLoop* event_loop) {
    ClockSettingsModule* instance = malloc(sizeof(ClockSettingsModule));

    instance->timer = furi_event_loop_timer_alloc(
        event_loop, clock_settings_module_timer_callback, FuriEventLoopTimerTypePeriodic, instance);
    instance->view = view_alloc();
    view_set_enter_callback(instance->view, clock_settings_module_view_enter_callback);
    view_set_exit_callback(instance->view, clock_settings_module_view_exit_callback);
    view_allocate_model(
        instance->view, ViewModelTypeLocking, sizeof(ClockSettingsModuleViewModel));
    with_view_model(
        instance->view, ClockSettingsModuleViewModel * model, { model->row = 0; }, false);
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, clock_settings_module_draw_callback);
    view_set_input_callback(instance->view, clock_settings_module_input_callback);

    return instance;
}

void clock_settings_module_free(ClockSettingsModule* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* clock_settings_module_get_view(ClockSettingsModule* instance) {
    furi_assert(instance);
    return instance->view;
}
