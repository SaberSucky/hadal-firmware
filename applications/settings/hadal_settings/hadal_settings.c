#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <storage/storage.h>
#include <desktop/desktop.h>
#include <desktop/desktop_settings.h>

#include "pack_info.h"

#define TAG        "HadalSettings"
#define MAX_PACKS  40u
/* the first entry of the pack list is "None": the built-in assets */
#define LIST_NONE  0u

typedef enum {
    ViewStart,
    ViewPacks,
    ViewPackInfo,
} HadalView;

typedef enum {
    EventUsePack = 1,
} HadalEvent;

typedef struct {
    Gui* gui;
    Storage* storage;
    Desktop* desktop;
    ViewDispatcher* view_dispatcher;
    Submenu* start;
    Submenu* packs;
    Widget* info;

    HadalView view;
    char names[MAX_PACKS][HADAL_PACK_NAME_SIZE];
    size_t name_count;
    size_t chosen; /**< entry of the pack list being looked at, 0 is None */
    char in_use[HADAL_PACK_NAME_SIZE]; /**< the setting, empty for none */
} HadalSettings;

/* ---- the asset pack list ---- */

static void read_setting(HadalSettings* app) {
    DesktopSettings* settings = malloc(sizeof(DesktopSettings));
    desktop_api_get_settings(app->desktop, settings);
    strlcpy(app->in_use, settings->active_asset_pack, sizeof(app->in_use));
    free(settings);
}

static void write_setting(HadalSettings* app, const char* name) {
    DesktopSettings* settings = malloc(sizeof(DesktopSettings));
    desktop_api_get_settings(app->desktop, settings);
    strlcpy(settings->active_asset_pack, name, sizeof(settings->active_asset_pack));
    desktop_api_set_settings(app->desktop, settings);
    free(settings);
    strlcpy(app->in_use, name, sizeof(app->in_use));
}

static void packs_callback(void* context, uint32_t index) {
    HadalSettings* app = context;
    app->chosen = index;
    view_dispatcher_send_custom_event(app->view_dispatcher, index + 100);
}

static void packs_rebuild(HadalSettings* app, uint32_t select) {
    submenu_reset(app->packs);
    submenu_set_header(app->packs, "Asset Pack");

    char label[HADAL_PACK_NAME_SIZE + 8];
    snprintf(label, sizeof(label), "%sNone (built-in)", app->in_use[0] ? "  " : "* ");
    submenu_add_item(app->packs, label, LIST_NONE, packs_callback, app);
    for(size_t i = 0; i < app->name_count; i++) {
        bool current = strcmp(app->names[i], app->in_use) == 0;
        snprintf(label, sizeof(label), "%s%s", current ? "* " : "  ", app->names[i]);
        submenu_add_item(app->packs, label, i + 1, packs_callback, app);
    }
    submenu_set_selected_item(app->packs, select);
}

static void info_use_callback(GuiButtonType result, InputType type, void* context) {
    HadalSettings* app = context;
    if(result == GuiButtonTypeCenter && type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, EventUsePack);
    }
}

static void info_show(HadalSettings* app) {
    widget_reset(app->info);
    const bool none = app->chosen == LIST_NONE;
    const char* name = none ? "Built-in" : app->names[app->chosen - 1];
    const bool current = none ? app->in_use[0] == '\0' : strcmp(name, app->in_use) == 0;

    widget_add_string_element(app->info, 64, 2, AlignCenter, AlignTop, FontPrimary, name);

    FuriString* text = furi_string_alloc();
    if(none) {
        furi_string_set(text, "The animations, icons and\nfonts the firmware has.");
    } else {
        HadalPackInfo info;
        hadal_pack_info_scan(app->storage, name, &info);
        furi_string_printf(
            text,
            "Animations: %u\nIcons: %u\nFonts: %u",
            info.animations,
            info.icons,
            info.fonts);
        if(!info.animations && !info.icons && !info.fonts) {
            furi_string_cat(text, "\nNothing in it the firmware\ncan use.");
        }
    }
    widget_add_string_multiline_element(
        app->info, 64, 16, AlignCenter, AlignTop, FontSecondary, furi_string_get_cstr(text));
    furi_string_free(text);

    widget_add_button_element(
        app->info,
        GuiButtonTypeCenter,
        current ? "In use" : "Use",
        current ? NULL : info_use_callback,
        app);
}

/* ---- start page ---- */

static void start_callback(void* context, uint32_t index) {
    HadalSettings* app = context;
    if(index == 0) view_dispatcher_send_custom_event(app->view_dispatcher, 50);
}

/* ---- navigation ---- */

static bool custom_event_callback(void* context, uint32_t event) {
    HadalSettings* app = context;
    if(event == 50) { // Asset Pack
        app->name_count = hadal_pack_list(app->storage, app->names, MAX_PACKS);
        read_setting(app);
        uint32_t select = LIST_NONE;
        for(size_t i = 0; i < app->name_count; i++) {
            if(strcmp(app->names[i], app->in_use) == 0) select = i + 1;
        }
        packs_rebuild(app, select);
        app->view = ViewPacks;
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewPacks);
    } else if(event >= 100) { // a pack was picked
        info_show(app);
        app->view = ViewPackInfo;
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewPackInfo);
    } else if(event == EventUsePack) {
        write_setting(app, app->chosen == LIST_NONE ? "" : app->names[app->chosen - 1]);
        packs_rebuild(app, app->chosen);
        app->view = ViewPacks;
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewPacks);
    }
    return true;
}

static bool navigation_callback(void* context) {
    HadalSettings* app = context;
    if(app->view == ViewPackInfo) {
        app->view = ViewPacks;
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewPacks);
        return true;
    }
    if(app->view == ViewPacks) {
        app->view = ViewStart;
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewStart);
        return true;
    }
    return false; // leave the app
}

/* ---- the app ---- */

int32_t hadal_settings_app(void* p) {
    UNUSED(p);
    HadalSettings* app = malloc(sizeof(HadalSettings));
    memset(app, 0, sizeof(*app));

    app->storage = furi_record_open(RECORD_STORAGE);
    app->desktop = furi_record_open(RECORD_DESKTOP);
    app->gui = furi_record_open(RECORD_GUI);

    app->view_dispatcher = view_dispatcher_alloc();
    app->start = submenu_alloc();
    app->packs = submenu_alloc();
    app->info = widget_alloc();

    submenu_set_header(app->start, "Hadal");
    submenu_add_item(app->start, "Asset Pack", 0, start_callback, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, navigation_callback);
    view_dispatcher_add_view(app->view_dispatcher, ViewStart, submenu_get_view(app->start));
    view_dispatcher_add_view(app->view_dispatcher, ViewPacks, submenu_get_view(app->packs));
    view_dispatcher_add_view(app->view_dispatcher, ViewPackInfo, widget_get_view(app->info));
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->view = ViewStart;
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewStart);
    view_dispatcher_run(app->view_dispatcher);

    view_dispatcher_remove_view(app->view_dispatcher, ViewStart);
    view_dispatcher_remove_view(app->view_dispatcher, ViewPacks);
    view_dispatcher_remove_view(app->view_dispatcher, ViewPackInfo);
    submenu_free(app->start);
    submenu_free(app->packs);
    widget_free(app->info);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_DESKTOP);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
