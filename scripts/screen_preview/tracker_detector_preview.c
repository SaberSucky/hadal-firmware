#include "ble_preview_common.h"
#include "../../applications/system/ble_tools/tracker_detector.c"

#define MIN_MS (60U * 1000U)

static TrackerDetector* fresh(void) {
    static TrackerDetector app;
    memset(&app, 0, sizeof(app));
    app.table = (BleSeenTable){app.devices, CAPACITY, 0};
    app.supported = true;
    return &app;
}

int main(int argc, char** argv) {
    if(argc > 1) preview_set_dir(argv[1]);
    uint8_t data[31];
    size_t length;

    TrackerDetector* app = fresh();
    preview_tick = 7 * MIN_MS;
    preview_draw("tracker_detector_empty", draw_callback, app);

    app = fresh();
    length = adv_hex(AIRTAG_AWAY, data);
    hear(&app->table, 1, data, length, -71, 0, 14 * MIN_MS);
    length = adv_hex(AIRTAG_HOME, data);
    hear(&app->table, 2, data, length, -60, 2 * MIN_MS, 14 * MIN_MS);
    length = adv_hex(TILE, data);
    hear(&app->table, 3, data, length, -88, 11 * MIN_MS, 14 * MIN_MS);
    length = adv_hex(SMARTTAG, data);
    hear(&app->table, 4, data, length, -80, 12 * MIN_MS, 14 * MIN_MS);
    length = adv_hex(AIRTAG_AWAY, data);
    hear(&app->table, 5, data, length, -93, 13 * MIN_MS, 14 * MIN_MS);
    preview_tick = 14 * MIN_MS;
    tracker_sort(app);
    scroll_to_selection(app);
    preview_draw("tracker_detector_list", draw_callback, app);

    app->selected = 4;
    scroll_to_selection(app);
    preview_draw("tracker_detector_list_scrolled", draw_callback, app);

    app->selected = 0;
    scroll_to_selection(app);
    check_following(app, preview_tick);
    preview_draw("tracker_detector_warning", draw_callback, app);

    app->warning = false;
    start_locating(app, &app->devices[app->order[0]]);
    for(int i = 0; i < 60; i++) {
        ble_meter_add(&app->meter, (int8_t)(-90 + i / 2 + (i * 7) % 5), preview_tick);
    }
    preview_draw("tracker_detector_locate", draw_callback, app);

    app->supported = false;
    preview_draw("ble_unsupported", draw_callback, app);
    return 0;
}
