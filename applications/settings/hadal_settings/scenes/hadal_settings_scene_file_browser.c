#include "../hadal_settings_i.h"

#include <saved_struct.h>
#include "../../../services/gui/modules/file_browser_sort.h"

/* How Archive and file pickers sort, and whether Archive shows hidden files */

static const char* const sort_texts[FileBrowserSortCount] = {
    [FileBrowserSortName] = "Name",
    [FileBrowserSortNameReverse] = "Name Z-A",
    [FileBrowserSortNewest] = "Newest",
    [FileBrowserSortLargest] = "Largest",
    [FileBrowserSortType] = "Type",
};
static const char* const folder_texts[] = {"First", "Mixed"};
static const char* const hidden_texts[] = {"Show", "Hide"};

static FileBrowserSortPrefs prefs;

static void load(void) {
    if(!saved_struct_load(
           FILE_BROWSER_SORT_PREFS_PATH,
           &prefs,
           sizeof(prefs),
           FILE_BROWSER_SORT_PREFS_MAGIC,
           FILE_BROWSER_SORT_PREFS_VERSION) ||
       prefs.sort_by >= FileBrowserSortCount) {
        memset(&prefs, 0, sizeof(prefs));
    }
}

static void save(void) {
    saved_struct_save(
        FILE_BROWSER_SORT_PREFS_PATH,
        &prefs,
        sizeof(prefs),
        FILE_BROWSER_SORT_PREFS_MAGIC,
        FILE_BROWSER_SORT_PREFS_VERSION);
}

static void sort_changed(VariableItem* item) {
    prefs.sort_by = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, sort_texts[prefs.sort_by]);
    save();
}

static void folders_changed(VariableItem* item) {
    prefs.folders_mixed = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, folder_texts[prefs.folders_mixed]);
    save();
}

static void hidden_changed(VariableItem* item) {
    prefs.hide_hidden = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, hidden_texts[prefs.hide_hidden]);
    save();
}

void hadal_settings_scene_file_browser_on_enter(void* context) {
    HadalSettings* app = context;
    load();
    variable_item_list_reset(app->variable_list);

    VariableItem* item = variable_item_list_add(
        app->variable_list, "Sort by", FileBrowserSortCount, sort_changed, app);
    variable_item_set_current_value_index(item, prefs.sort_by);
    variable_item_set_current_value_text(item, sort_texts[prefs.sort_by]);
    variable_item_set_values_text(item, sort_texts);

    item = variable_item_list_add(app->variable_list, "Folders", 2, folders_changed, app);
    variable_item_set_current_value_index(item, prefs.folders_mixed ? 1 : 0);
    variable_item_set_current_value_text(item, folder_texts[prefs.folders_mixed ? 1 : 0]);
    variable_item_set_values_text(item, folder_texts);

    // Archive's: an app's own file picker shows or hides them as the app asks
    item = variable_item_list_add(app->variable_list, "Hidden in Archive", 2, hidden_changed, app);
    variable_item_set_current_value_index(item, prefs.hide_hidden ? 1 : 0);
    variable_item_set_current_value_text(item, hidden_texts[prefs.hide_hidden ? 1 : 0]);
    variable_item_set_values_text(item, hidden_texts);

    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewVariableList);
}

bool hadal_settings_scene_file_browser_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void hadal_settings_scene_file_browser_on_exit(void* context) {
    HadalSettings* app = context;
    variable_item_list_reset(app->variable_list);
}
