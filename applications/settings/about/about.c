#include <furi.h>

#include <gui/gui.h>
#include <gui/elements.h>
#include <gui/view_port.h>
#include <assets_icons.h>

#include <furi_hal_version.h>
#include <furi_hal_region.h>
#include <furi_hal_bt.h>
#include <furi_hal_info.h>
#include <toolbox/version.h>

#include "about_icons.h"

/* About, as every Hadal page: a header, up to three lines between it and the hint bar, and Left
 * and Right going through the pages ("1 of 9"). The first page is Abyss and which Hadal this is */

#define ABOUT_LINES       3
#define ABOUT_LINE_HEIGHT 10
#define ABOUT_FIRST_LINE  26 // the first line's baseline: under the header, a clear row over it

typedef struct {
    const char* title;
    FuriString* lines[ABOUT_LINES];
} AboutPage;

typedef struct {
    AboutPage pages[9];
    size_t count;
    size_t page;
    FuriString* hello; // the first page's lines, beside Abyss
} About;

static AboutPage* about_add(About* about, const char* title) {
    AboutPage* page = &about->pages[about->count++];
    page->title = title;
    for(size_t i = 0; i < ABOUT_LINES; i++) {
        page->lines[i] = furi_string_alloc();
    }
    return page;
}

static void about_build(About* about) {
    const Version* version = version_get();

    // 1: Abyss, and which Hadal this is (the date as every screen writes it, with dots)
    AboutPage* page = about_add(about, "About");
    FuriString* date = furi_string_alloc_set_str(version_get_builddate(version));
    furi_string_replace_all(date, "-", ".");
    furi_string_set_str(page->lines[0], "Hadal");
    furi_string_printf(
        page->lines[1],
        "%s%s",
        version_get_dirty_flag(version) ? "! " : "",
        version_get_githash(version));
    furi_string_set(page->lines[2], date);
    furi_string_free(date);

    page = about_add(about, "Hadal");
    furi_string_set_str(page->lines[0], "For experimental use only.");
    furi_string_set_str(page->lines[1], "Not for illegal use: we");
    furi_string_set_str(page->lines[2], "do not condone any of it.");

    page = about_add(about, "License");
    furi_string_set_str(page->lines[0], "Free and open source,");
    furi_string_set_str(page->lines[1], "under the GPLv3. If you");
    furi_string_set_str(page->lines[2], "paid for it, you were had.");

    page = about_add(about, "Credits");
    furi_string_set_str(page->lines[0], "Built on Unleashed and");
    furi_string_set_str(page->lines[1], "Flipper's own firmware.");
    furi_string_set_str(page->lines[2], "github.com/SaberSucky");

    page = about_add(about, "Firmware");
    uint16_t api_major, api_minor;
    furi_hal_info_get_api_version(&api_major, &api_minor);
    const BleGlueC2Info* c2 = ble_glue_get_c2_info();
    furi_string_printf(
        page->lines[0], "%s, %s", version_get_version(version), version_get_gitbranch(version));
    furi_string_printf(
        page->lines[1], "%s, API %u.%u", version_get_githash(version), api_major, api_minor);
    furi_string_printf(
        page->lines[2],
        "Radio %s, target %d",
        c2 ? c2->StackTypeString : "none",
        version_get_target(version));

    page = about_add(about, "Hardware");
    const char* name = furi_hal_version_get_name_ptr();
    furi_string_printf(
        page->lines[0],
        "%s, %d.F%dB%dC%d %s",
        name ? name : "Unknown",
        furi_hal_version_get_hw_version(),
        furi_hal_version_get_hw_target(),
        furi_hal_version_get_hw_body(),
        furi_hal_version_get_hw_connect(),
        furi_hal_version_get_hw_region_name_otp());
    furi_string_set_str(page->lines[1], "Serial number:");
    const uint8_t* uid = furi_hal_version_uid();
    for(size_t i = 0; i < furi_hal_version_uid_size(); i++) {
        furi_string_cat_printf(page->lines[2], "%02X", uid[i]);
    }

    page = about_add(about, "Product");
    furi_string_printf(
        page->lines[0],
        "%s, %s",
        furi_hal_version_get_model_name(),
        furi_hal_version_get_model_code());
    furi_string_printf(page->lines[1], "FCC ID: %s", furi_hal_version_get_fcc_id());
    furi_string_printf(page->lines[2], "IC: %s", furi_hal_version_get_ic_id());

    page = about_add(about, "Compliance");
    furi_string_set_str(page->lines[0], "Every certificate is at");
    furi_string_set_str(page->lines[1], "flipp.dev/compliance");

    page = about_add(about, "Made by");
    furi_string_set_str(page->lines[0], "Flipper Devices Inc., Suite B");
    furi_string_set_str(page->lines[1], "#551, 2803 Philadelphia Pike");
    furi_string_set_str(page->lines[2], "Claymont, DE 19703, USA");
}

static void about_draw(Canvas* canvas, void* context) {
    About* about = context;
    const AboutPage* page = &about->pages[about->page];
    canvas_clear(canvas);
    elements_header(canvas, page->title);

    if(about->page == 0) {
        // Abyss beside the name, the commit and the date
        canvas_draw_icon(canvas, 4, 17, &I_Abyss_32x32);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 42, ABOUT_FIRST_LINE, furi_string_get_cstr(page->lines[0]));
        canvas_set_font(canvas, FontSecondary);
        for(size_t i = 1; i < ABOUT_LINES; i++) {
            canvas_draw_str(
                canvas,
                42,
                ABOUT_FIRST_LINE + i * ABOUT_LINE_HEIGHT,
                furi_string_get_cstr(page->lines[i]));
        }
    } else {
        canvas_set_font(canvas, FontSecondary);
        for(size_t i = 0; i < ABOUT_LINES; i++) {
            FuriString* line = furi_string_alloc_set(page->lines[i]);
            elements_string_fit_width(canvas, line, 122);
            canvas_draw_str(
                canvas, 3, ABOUT_FIRST_LINE + i * ABOUT_LINE_HEIGHT, furi_string_get_cstr(line));
            furi_string_free(line);
        }
    }

    char hints[24];
    snprintf(hints, sizeof(hints), "lr=%u of %u", about->page + 1, about->count);
    elements_hint_bar(canvas, hints);
}

static void about_input(InputEvent* event, void* context) {
    FuriMessageQueue* queue = context;
    furi_message_queue_put(queue, event, FuriWaitForever);
}

int32_t about_settings_app(void* p) {
    UNUSED(p);
    About* about = malloc(sizeof(About));
    about_build(about);

    FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, about_draw, about);
    view_port_input_callback_set(view_port, about_input, queue);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    InputEvent event;
    bool running = true;
    while(running && furi_message_queue_get(queue, &event, FuriWaitForever) == FuriStatusOk) {
        if(event.type != InputTypeShort && event.type != InputTypeRepeat) continue;
        if(event.key == InputKeyBack) {
            running = false;
        } else if(event.key == InputKeyRight || event.key == InputKeyOk) {
            // OK goes on, as Next did; round from the last page to the first
            about->page = (about->page + 1) % about->count;
            view_port_update(view_port);
        } else if(event.key == InputKeyLeft) {
            about->page = about->page ? about->page - 1 : about->count - 1;
            view_port_update(view_port);
        }
    }

    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(queue);
    for(size_t i = 0; i < about->count; i++) {
        for(size_t j = 0; j < ABOUT_LINES; j++) {
            furi_string_free(about->pages[i].lines[j]);
        }
    }
    free(about);
    return 0;
}
