#include "../archive_i.h"
#include "../helpers/archive_browser.h"

#define TAG "Archive"

void archive_scene_info_widget_callback(GuiButtonType result, InputType type, void* context) {
    furi_assert(context);
    ArchiveApp* app = (ArchiveApp*)context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, result);
    }
}

void archive_scene_info_on_enter(void* context) {
    furi_assert(context);
    ArchiveApp* instance = context;

    FuriString* filename;
    FuriString* dirname;
    FuriString* str_size;
    filename = furi_string_alloc();
    dirname = furi_string_alloc();
    str_size = furi_string_alloc();

    ArchiveFile_t* current = archive_get_current_file(instance->browser);
    char file_info_message[128];
    Storage* fs_api = furi_record_open(RECORD_STORAGE);

    // Nothing selected: the fields stay empty rather than read through a null file
    FuriString* current_path = furi_string_alloc();
    if(current) {
        furi_string_set(current_path, current->path);
    }

    // the file's name heads the page, as every page's header does; Back returns to the list
    path_extract_filename(current_path, filename, false);
    widget_add_header_element(instance->widget, furi_string_get_cstr(filename));

    // Directory path
    path_extract_dirname(furi_string_get_cstr(current_path), dirname);
    if(strcmp(furi_string_get_cstr(dirname), "/any") == 0) {
        furi_string_replace(dirname, STORAGE_ANY_PATH_PREFIX, "/");
    } else {
        furi_string_replace(dirname, STORAGE_ANY_PATH_PREFIX, "");
    }

    // File size
    FileInfo fileinfo = {0}; // left as is by a failed stat
    storage_common_stat(fs_api, furi_string_get_cstr(current_path), &fileinfo);
    if(fileinfo.size <= 1024) {
        furi_string_printf(str_size, "%lld", fileinfo.size);
        snprintf(
            file_info_message,
            sizeof(file_info_message),
            "Size: \e#%s\e# bytes\n%s",
            furi_string_get_cstr(str_size),
            furi_string_get_cstr(dirname));
    } else {
        furi_string_printf(str_size, "%lld", fileinfo.size / 1024);
        snprintf(
            file_info_message,
            sizeof(file_info_message),
            "Size: \e#%s\e# KB\n%s",
            furi_string_get_cstr(str_size),
            furi_string_get_cstr(dirname));
    }
    widget_add_text_box_element(
        instance->widget, 3, 18, 122, 26, AlignLeft, AlignTop, file_info_message, true);

    // This one to return and cursor select this file
    path_extract_filename_no_ext(furi_string_get_cstr(current_path), filename);
    strlcpy(instance->text_store, furi_string_get_cstr(filename), MAX_NAME_LEN);

    furi_string_free(current_path);
    furi_string_free(filename);
    furi_string_free(dirname);
    furi_string_free(str_size);

    view_dispatcher_switch_to_view(instance->view_dispatcher, ArchiveViewWidget);
}

bool archive_scene_info_on_event(void* context, SceneManagerEvent event) {
    furi_assert(context);
    ArchiveApp* app = (ArchiveApp*)context;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_next_scene(app->scene_manager, ArchiveAppSceneBrowser);
        return true;
    }
    return false;
}

void archive_scene_info_on_exit(void* context) {
    furi_assert(context);
    ArchiveApp* app = (ArchiveApp*)context;

    widget_reset(app->widget);
}
