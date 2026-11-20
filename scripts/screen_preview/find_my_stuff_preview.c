#include "ble_preview_common.h"
#include "../../applications/system/ble_tools/find_my_stuff.c"

static FindMyStuff* fresh(void) {
    static FindMyStuff app;
    memset(&app, 0, sizeof(app));
    app.table = (BleSeenTable){app.devices, CAPACITY, 0};
    app.supported = true;
    return &app;
}

int main(int argc, char** argv) {
    if(argc > 1) preview_set_dir(argv[1]);
    uint8_t data[31];
    size_t length;

    FindMyStuff* app = fresh();
    preview_tick = 3000;
    preview_draw("find_my_stuff_empty", draw_callback, app);

    app = fresh();
    length = adv_name("Galaxy Buds2 Pro", data);
    hear(&app->table, 1, data, length, -58, 0, 20000);
    length = adv_name("WH-1000XM5 Headphon", data);
    hear(&app->table, 2, data, length, -70, 0, 20000);
    length = adv_hex(AIRPODS, data);
    hear(&app->table, 3, data, length, -49, 0, 20000);
    length = adv_hex(APPLE, data);
    hear(&app->table, 4, data, length, -81, 0, 20000);
    hear(&app->table, 5, NULL, 0, -95, 0, 20000);
    length = adv_name("Mi Band 7", data);
    hear(&app->table, 6, data, length, -77, 0, 20000);
    length = adv_hex(SAMSUNG, data);
    hear(&app->table, 7, data, length, -86, 0, 20000);
    preview_tick = 20000;
    app->selected = 1;
    resort(app);
    preview_draw("find_my_stuff_list", draw_callback, app);

    select_row(app, 0);
    app->mode = ModeLocate;
    memcpy(app->locate_addr, device_at(app, 0)->addr, BLE_SEEN_ADDR_SIZE);
    snprintf(app->locate_label, sizeof(app->locate_label), "Galaxy Buds2 Pro");
    ble_meter_reset(&app->meter);
    for(int i = 0; i < 100; i++) {
        ble_meter_add(&app->meter, (int8_t)(-85 + i / 3 + (i * 7) % 6), preview_tick);
    }
    app->sound = true;
    preview_draw("find_my_stuff_locate", draw_callback, app);

    ble_meter_reset(&app->meter);
    preview_draw("find_my_stuff_locate_listening", draw_callback, app);

    for(int i = 0; i < 30; i++) {
        ble_meter_add(&app->meter, (int8_t)(-60 - i / 2), preview_tick - 9000);
    }
    app->sound = false;
    preview_draw("find_my_stuff_locate_lost", draw_callback, app);
    return 0;
}
