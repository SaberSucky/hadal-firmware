#include "preview.h"
#include "../../applications/system/battery_history/battery_history.c"

#define HOUR 3600U

static BatteryHistory app;
static BatteryLogRecord records[4096];

static void add(size_t* count, uint32_t t, int charge, bool charging, bool start) {
    records[(*count)++] = (BatteryLogRecord){
        .timestamp = t,
        .charge = (uint8_t)charge,
        .flags = (charging ? BatteryLogFlagCharging : 0) | (start ? BatteryLogFlagStart : 0),
    };
}

static void show(const char* name, size_t count, size_t span) {
    memset(&app, 0, sizeof(app));
    app.mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app.records = records;
    app.count = count;
    app.span = span;
    app.has_estimate = battery_stats_estimate(records, count, &app.estimate);
    compute_columns(&app);
    preview_draw(name, draw_callback, &app);
    furi_mutex_free(app.mutex);
}

int main(int argc, char** argv) {
    if(argc > 1) preview_set_dir(argv[1]);
    const uint32_t start = 1700000000U;
    size_t count = 0;

    // just installed: one reading
    add(&count, start, 64, false, true);
    show("battery_history_new", count, 0);

    // ten days: drains about 30% a day, charged every few days, off for a night
    count = 0;
    int charge = 100;
    uint32_t t = start;
    bool first = true;
    for(int day = 0; day < 10; day++) {
        for(int step = 0; step < 144; step++, t += 600) {
            const bool off = day == 4 && step >= 20 && step < 70;
            if(off) {
                first = true;
                continue;
            }
            const bool charging = (day == 3 && step >= 100 && step < 118) || (day == 7 && step >= 30 && step < 50);
            if(charging) {
                charge += 5;
                if(charge > 100) charge = 100;
            } else if(step % 5 == 0 && charge > 3) {
                charge -= 1;
            }
            add(&count, t, charge, charging, first);
            first = false;
        }
    }
    show("battery_history_day", count, 0);
    show("battery_history_week", count, 2);
    show("battery_history_two_weeks", count, 3);

    add(&count, t, charge, true, false);
    show("battery_history_charging", count, 1);
    return 0;
}
