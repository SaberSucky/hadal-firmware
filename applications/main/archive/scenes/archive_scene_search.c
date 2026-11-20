#include "../archive_i.h"
#include "../helpers/archive_browser.h"
#include "../helpers/archive_search.h"

#include <gui/modules/file_browser_sort.h>

/* Search: a name, typed; then every file and folder under the current folder whose name holds
 * it. Picking one opens its folder in the Browser tab, on it. */

#define SEARCH_LIMIT (20000U) /* entries looked at, at most: a few seconds on a full card */

enum {
    SearchEventTyped = 1000,
};

static void archive_scene_search_typed(void* context) {
    ArchiveApp* archive = context;
    view_dispatcher_send_custom_event(archive->view_dispatcher, SearchEventTyped);
}

static void archive_scene_search_picked(void* context, uint32_t index) {
    ArchiveApp* archive = context;
    view_dispatcher_send_custom_event(archive->view_dispatcher, index);
}

static void archive_scene_search_clear(ArchiveApp* archive) {
    for(size_t i = 0; i < archive->search_count; i++) {
        furi_string_free(archive->search_results[i]);
    }
    archive->search_count = 0;
}

static void archive_scene_search_show_results(ArchiveApp* archive) {
    submenu_reset(archive->submenu);
    FuriString* header = furi_string_alloc();
    if(archive->search_count) {
        furi_string_printf(
            header,
            "%u%s found",
            (unsigned)archive->search_count,
            archive->search_count == ARCHIVE_SEARCH_MAX_RESULTS ? "+" : "");
    } else {
        furi_string_printf(header, "Nothing named \"%s\"", archive->search_text);
    }
    submenu_set_header(archive->submenu, furi_string_get_cstr(header));
    furi_string_free(header);
    for(size_t i = 0; i < archive->search_count; i++) {
        // the name, and the folder it is in when there is room
        const char* path = furi_string_get_cstr(archive->search_results[i]);
        const char* name = strrchr(path, '/');
        submenu_add_item(
            archive->submenu, name ? name + 1 : path, i, archive_scene_search_picked, archive);
    }
    view_dispatcher_switch_to_view(archive->view_dispatcher, ArchiveViewSubmenu);
}

void archive_scene_search_on_enter(void* context) {
    ArchiveApp* archive = context;
    TextInput* text_input = archive->text_input;
    // whatever another page left on the keyboard (a validator) goes
    text_input_reset(text_input);
    text_input_set_header_text(text_input, "Find files named:");
    text_input_set_result_callback(
        text_input,
        archive_scene_search_typed,
        archive,
        archive->search_text,
        sizeof(archive->search_text),
        false);
    view_dispatcher_switch_to_view(archive->view_dispatcher, ArchiveViewTextInput);
}

bool archive_scene_search_on_event(void* context, SceneManagerEvent event) {
    ArchiveApp* archive = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == SearchEventTyped) {
        archive_scene_search_clear(archive);
        view_dispatcher_switch_to_view(archive->view_dispatcher, ArchiveViewStack);
        archive_show_loading_popup(archive, true);
        // from the folder being looked at, or the whole card from the top
        const char* root = furi_string_get_cstr(archive->browser->path);
        if(strncmp(root, STORAGE_EXT_PATH_PREFIX, strlen(STORAGE_EXT_PATH_PREFIX)) != 0) {
            root = STORAGE_EXT_PATH_PREFIX;
        }
        file_browser_sort_prefs_load();
        Storage* storage = furi_record_open(RECORD_STORAGE);
        archive->search_count = archive_search(
            storage,
            root,
            archive->search_text,
            file_browser_sort_prefs.hide_hidden,
            archive->search_results,
            ARCHIVE_SEARCH_MAX_RESULTS,
            SEARCH_LIMIT);
        furi_record_close(RECORD_STORAGE);
        archive_show_loading_popup(archive, false);
        archive_scene_search_show_results(archive);
    } else if(event.event < archive->search_count) {
        archive_show_file(
            archive->browser, furi_string_get_cstr(archive->search_results[event.event]));
        scene_manager_search_and_switch_to_previous_scene(
            archive->scene_manager, ArchiveAppSceneBrowser);
    }
    return true;
}

void archive_scene_search_on_exit(void* context) {
    ArchiveApp* archive = context;
    submenu_reset(archive->submenu);
    text_input_reset(archive->text_input);
    archive_scene_search_clear(archive);
}
