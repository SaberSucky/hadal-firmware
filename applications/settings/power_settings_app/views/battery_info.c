#include "battery_info.h"
#include <furi.h>
#include <gui/elements.h>
#include <assets_icons.h>
#include <locale/locale.h>

#define LOW_CHARGE_THRESHOLD         (10)
#define HIGH_DRAIN_CURRENT_THRESHOLD (-100)

/* The history page: the charge as a line over a shaded area, 0% at the baseline, clear of the
 * scrollbar at the screen's right edge */
#define GRAPH_X      2
#define GRAPH_WIDTH  122U
#define GRAPH_TOP    29 /* 100% */
#define GRAPH_BOTTOM 60 /* 0% */
#define SPANS        4

typedef struct {
    uint32_t seconds;
    const char* name;
} BatterySpan;

static const BatterySpan spans[SPANS] = {
    {24U * 3600U, "1 day"},
    {3U * 24U * 3600U, "3 days"},
    {7U * 24U * 3600U, "1 week"},
    {14U * 24U * 3600U, "2 weeks"},
};

typedef enum {
    BatteryInfoPageNow,
    BatteryInfoPageHistory,
} BatteryInfoPage;

typedef struct {
    BatteryInfoModel now;
    BatteryInfoPage page;
    uint8_t span;
    bool history; /**< the records were read */
    size_t records;
    int8_t charge[SPANS][GRAPH_WIDTH];
    bool has_estimate;
    BatteryEstimate estimate;
    uint8_t last_charge; /**< the reading now, from the records */
    bool last_charging;
} BatteryInfoViewModel;

struct BatteryInfo {
    View* view;
};

static void draw_stat(Canvas* canvas, int x, int y, const Icon* icon, char* val) {
    canvas_draw_frame(canvas, x - 7, y + 7, 30, 13);
    canvas_draw_icon(canvas, x, y, icon);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, x - 4, y + 16, 24, 6);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_str_aligned(canvas, x + 8, y + 22, AlignCenter, AlignBottom, val);
}

static void draw_battery(Canvas* canvas, BatteryInfoModel* data, int x, int y) {
    char emote[20] = {};
    char header[20] = {};
    char value[20] = {};

    int32_t current = 1000.0f * data->gauge_current;

    // Draw battery
    canvas_draw_icon(canvas, x, y, &I_BatteryBody_52x28);
    if(current > 0) {
        canvas_draw_icon(canvas, x + 16, y + 7, &I_FaceCharging_29x14);
    } else if(current < HIGH_DRAIN_CURRENT_THRESHOLD) {
        canvas_draw_icon(canvas, x + 16, y + 7, &I_FaceConfused_29x14);
    } else if(data->charge < LOW_CHARGE_THRESHOLD) {
        canvas_draw_icon(canvas, x + 16, y + 7, &I_FaceNopower_29x14);
    } else {
        canvas_draw_icon(canvas, x + 16, y + 7, &I_FaceNormal_29x14);
    }

    // Draw bubble: it ends clear of the scrollbar
    elements_bubble(canvas, 53, 0, 69, 39);

    // Set text
    if(current > 0) {
        snprintf(emote, sizeof(emote), "%s", "Yummy!");
        snprintf(header, sizeof(header), "%s", "Charging at");
        snprintf(
            value,
            sizeof(value),
            "%lu.%luV   %lumA",
            (uint32_t)(data->vbus_voltage),
            (uint32_t)(data->vbus_voltage * 10) % 10,
            current);
    } else if(current < -5) {
        // 0-5ma deadband
        snprintf(
            emote,
            sizeof(emote),
            "%s",
            current < HIGH_DRAIN_CURRENT_THRESHOLD ? "Oh no!" : "Om-nom-nom!");
        snprintf(header, sizeof(header), "%s", "Consumption is");
        snprintf(
            value,
            sizeof(value),
            "%ld %s",
            ABS(current),
            current < HIGH_DRAIN_CURRENT_THRESHOLD ? "mA!" : "mA");
    } else if(data->vbus_voltage > 0) {
        if(data->charge_voltage_limit < 4.2f) {
            // Non-default battery charging limit, mention it
            snprintf(emote, sizeof(emote), "Charged!");
            snprintf(header, sizeof(header), "Limited to");
            snprintf(
                value,
                sizeof(value),
                "%lu.%luV",
                (uint32_t)(data->charge_voltage_limit),
                (uint32_t)(data->charge_voltage_limit * 10) % 10);
        } else {
            snprintf(header, sizeof(header), "Charged!");
        }
    } else {
        snprintf(header, sizeof(header), "Napping...");
    }

    canvas_draw_str_aligned(canvas, 91, y + 3, AlignCenter, AlignCenter, emote);
    canvas_draw_str_aligned(canvas, 91, y + 15, AlignCenter, AlignCenter, header);
    canvas_draw_str_aligned(canvas, 91, y + 27, AlignCenter, AlignCenter, value);
}

static void draw_now(Canvas* canvas, BatteryInfoModel* model) {
    draw_battery(canvas, model, 0, 5);

    char batt_level[10];
    char temperature[10];
    char voltage[10];
    char health[10];

    snprintf(batt_level, sizeof(batt_level), "%lu%%", (uint32_t)model->charge);
    if(locale_get_measurement_unit() == LocaleMeasurementUnitsMetric) {
        snprintf(temperature, sizeof(temperature), "%lu C", (uint32_t)model->gauge_temperature);
    } else {
        snprintf(
            temperature,
            sizeof(temperature),
            "%lu F",
            (uint32_t)locale_celsius_to_fahrenheit(model->gauge_temperature));
    }
    snprintf(
        voltage,
        sizeof(voltage),
        "%lu.%01lu V",
        (uint32_t)model->gauge_voltage,
        (uint32_t)(model->gauge_voltage * 10) % 10UL);
    snprintf(health, sizeof(health), "%d%%", model->health);

    // a column apart, a column in from the left: the scrollbar has the screen's right edge
    draw_stat(canvas, 8, 42, &I_Battery_16x16, batt_level);
    draw_stat(canvas, 39, 42, &I_Temperature_16x16, temperature);
    draw_stat(canvas, 70, 42, &I_Voltage_16x16, voltage);
    draw_stat(canvas, 101, 42, &I_Health_16x16, health);
}

static int graph_y(int charge) {
    return GRAPH_BOTTOM - charge * (GRAPH_BOTTOM - GRAPH_TOP) / 100;
}

static void draw_history(Canvas* canvas, BatteryInfoViewModel* model) {
    char text[48];
    elements_header(canvas, "History");

    // the span at the header's end, on a clear patch over its line, with where it can go
    canvas_set_font(canvas, FontSecondary);
    const char* span = spans[model->span].name;
    const int32_t width = canvas_string_width(canvas, span);
    const int32_t right = 123;
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, right - width - 11, 0, width + 13, 13);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_str_aligned(canvas, right - 5, 11, AlignRight, AlignBottom, span);
    if(model->span > 0) {
        canvas_draw_icon(canvas, right - width - 10, 5, &I_ButtonLeftSmall_3x5);
    }
    if(model->span + 1 < SPANS) canvas_draw_icon(canvas, right - 3, 5, &I_ButtonRightSmall_3x5);

    // how it stands: the charge, and charging or how long it will last
    if(model->last_charging) {
        snprintf(text, sizeof(text), "%u%%, charging", model->last_charge);
    } else if(model->has_estimate) {
        char duration[24];
        battery_stats_format_duration(model->estimate.seconds_left, duration, sizeof(duration));
        snprintf(text, sizeof(text), "%u%%, %s left", model->last_charge, duration);
    } else if(model->records < 13) {
        snprintf(text, sizeof(text), "%u%%, still recording", model->last_charge);
    } else {
        snprintf(text, sizeof(text), "%u%%, on battery briefly", model->last_charge);
    }
    canvas_draw_str(canvas, 2, 25, text);

    // the baseline, and a dotted line through 50%
    canvas_draw_line(canvas, GRAPH_X, GRAPH_BOTTOM + 1, GRAPH_X + GRAPH_WIDTH - 1, GRAPH_BOTTOM + 1);
    for(int x = GRAPH_X; x < GRAPH_X + (int)GRAPH_WIDTH; x += 4) {
        canvas_draw_dot(canvas, x, graph_y(50));
    }

    // the charge: a line two rows thick over a lightly shaded area (a dot in eight: a full
    // battery's was a block), broken where it was off
    const int8_t* charge = model->charge[model->span];
    int last_x = -1, last_y = 0;
    bool any = false;
    for(int x = 0; x < (int)GRAPH_WIDTH; x++) {
        if(charge[x] == BATTERY_STATS_NO_DATA) {
            last_x = -1;
            continue;
        }
        any = true;
        const int column = GRAPH_X + x;
        const int y = graph_y(charge[x]);
        for(int fill = y + 2; fill <= GRAPH_BOTTOM; fill++) {
            if(!(column & 1) && !((GRAPH_BOTTOM - fill) & 3)) canvas_draw_dot(canvas, column, fill);
        }
        if(last_x >= 0) {
            canvas_draw_line(canvas, last_x, last_y, column, y);
            canvas_draw_line(canvas, last_x, last_y + 1, column, y + 1);
        } else {
            canvas_draw_line(canvas, column, y, column, y + 1);
        }
        last_x = column;
        last_y = y;
    }
    if(!any) {
        canvas_draw_str_aligned(
            canvas, GRAPH_X + GRAPH_WIDTH / 2, 44, AlignCenter, AlignCenter, "Nothing recorded yet");
    }
}

static void battery_info_draw_callback(Canvas* canvas, void* context) {
    furi_assert(context);
    BatteryInfoViewModel* model = context;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    if(model->page == BatteryInfoPageHistory) {
        draw_history(canvas, model);
    } else {
        draw_now(canvas, &model->now);
    }
    // the history is the page under this one
    elements_scrollbar(canvas, model->page, 2);
}

static bool battery_info_input_callback(InputEvent* event, void* context) {
    BatteryInfo* battery_info = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) {
        return event->key != InputKeyBack;
    }
    if(event->key == InputKeyBack) return false;
    with_view_model(
        battery_info->view,
        BatteryInfoViewModel * model,
        {
            if(event->key == InputKeyDown) {
                model->page = BatteryInfoPageHistory;
            } else if(event->key == InputKeyUp) {
                model->page = BatteryInfoPageNow;
            } else if(model->page == BatteryInfoPageHistory) {
                if(event->key == InputKeyRight && model->span + 1 < SPANS) model->span++;
                if(event->key == InputKeyLeft && model->span > 0) model->span--;
            }
        },
        true);
    return true;
}

BatteryInfo* battery_info_alloc(void) {
    BatteryInfo* battery_info = malloc(sizeof(BatteryInfo));
    battery_info->view = view_alloc();
    view_set_context(battery_info->view, battery_info);
    view_allocate_model(battery_info->view, ViewModelTypeLocking, sizeof(BatteryInfoViewModel));
    view_set_draw_callback(battery_info->view, battery_info_draw_callback);
    view_set_input_callback(battery_info->view, battery_info_input_callback);
    with_view_model(
        battery_info->view,
        BatteryInfoViewModel * model,
        {
            memset(model, 0, sizeof(BatteryInfoViewModel));
            memset(model->charge, BATTERY_STATS_NO_DATA, sizeof(model->charge));
        },
        false);

    return battery_info;
}

void battery_info_free(BatteryInfo* battery_info) {
    furi_assert(battery_info);
    view_free(battery_info->view);
    free(battery_info);
}

View* battery_info_get_view(BatteryInfo* battery_info) {
    furi_assert(battery_info);
    return battery_info->view;
}

void battery_info_set_data(BatteryInfo* battery_info, BatteryInfoModel* data) {
    furi_assert(battery_info);
    furi_assert(data);
    with_view_model(
        battery_info->view,
        BatteryInfoViewModel * model,
        { memcpy(&model->now, data, sizeof(BatteryInfoModel)); },
        true);
}

void battery_info_set_history(
    BatteryInfo* battery_info,
    const BatteryLogRecord* records,
    size_t count) {
    furi_assert(battery_info);
    if(!count) return;
    // the graph of every span at once: the records are not kept
    static bool charging[GRAPH_WIDTH]; // what the columns say of charging, not drawn
    with_view_model(
        battery_info->view,
        BatteryInfoViewModel * model,
        {
            const BatteryLogRecord* now = &records[count - 1];
            for(size_t span = 0; span < SPANS; span++) {
                battery_stats_columns(
                    records,
                    count,
                    now->timestamp,
                    spans[span].seconds,
                    model->charge[span],
                    charging,
                    GRAPH_WIDTH);
            }
            model->has_estimate = battery_stats_estimate(records, count, &model->estimate);
            model->records = count;
            model->last_charge = now->charge;
            model->last_charging = now->flags & BatteryLogFlagCharging;
            model->history = true;
        },
        true);
}
