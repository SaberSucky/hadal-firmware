#include "ble_preview_common.h"
#include "../../applications/system/ble_tools/crowd_meter.c"

static CrowdMeter* fresh(void) {
    static CrowdMeter app;
    memset(&app, 0, sizeof(app));
    app.table = (BleSeenTable){app.devices, CAPACITY, 0};
    app.supported = true;
    return &app;
}

int main(int argc, char** argv) {
    if(argc > 1) preview_set_dir(argv[1]);
    uint8_t data[31];

    CrowdMeter* app = fresh();
    preview_tick = 4000;
    preview_draw("crowd_meter_starting", draw_callback, app);

    app = fresh();
    const char* kinds[] = {APPLE, APPLE, AIRPODS, SAMSUNG, WINDOWS, TILE, NULL, NULL};
    const uint32_t end = 20U * 60U * 1000U;
    for(uint32_t t = 0; t <= end; t += 10000) {
        // a lunchtime rush: the crowd grows, peaks, and thins out
        const int phase = (int)(t / 10000);
        const int crowd = 6 + (phase < 70 ? phase / 2 : 35 - (phase - 70) / 2) + (phase * 13) % 5;
        for(int i = 0; i < crowd; i++) {
            const uint8_t id = (uint8_t)(i * 7 + 3);
            const char* hex = kinds[i % 8];
            const size_t length = hex ? adv_hex(hex, data) : 0;
            const uint8_t addr[6] = {id, (uint8_t)(i >> 8), 0x13, 0x37, 0xC0, 0xDE};
            ble_seen_update(&app->table, addr, -70, data, (uint8_t)length, t);
        }
        ble_seen_expire(&app->table, t, FORGET_MS, FORGET_MS);
        count_now(app, t);
        add_sample(app);
    }
    preview_tick = end;
    preview_draw("crowd_meter_now", draw_callback, app);
    app->page = PageKinds;
    preview_draw("crowd_meter_kinds", draw_callback, app);
    return 0;
}
