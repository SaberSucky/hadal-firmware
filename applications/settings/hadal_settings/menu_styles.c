#include "menu_styles.h"

#include <loader/loader.h>
#include <flipper_application/flipper_application.h>

#define MAX_STYLES 32u

void hadal_menu_styles_scan(HadalMenuStyles* styles, Storage* storage, size_t max_file) {
    styles->styles = NULL;
    styles->count = 0;

    File* dir = storage_file_alloc(storage);
    FuriString* path = furi_string_alloc();
    FuriString* name = furi_string_alloc();
    uint8_t* icon = malloc(FAP_MANIFEST_MAX_ICON_SIZE);
    char file_name[64];

    if(storage_dir_open(dir, LOADER_MENU_STYLES_PATH)) {
        while(styles->count < MAX_STYLES &&
              storage_dir_read(dir, NULL, file_name, sizeof(file_name))) {
            // the folder holds every loader plugin: menu styles are named by their appid
            const size_t length = strlen(file_name);
            if(strncmp(file_name, LOADER_MENU_STYLE_PREFIX, strlen(LOADER_MENU_STYLE_PREFIX)) !=
                   0 ||
               length < 5 || strcmp(file_name + length - 4, ".fal") != 0 || length >= max_file) {
                continue;
            }
            furi_string_printf(path, "%s/%s", LOADER_MENU_STYLES_PATH, file_name);
            if(!flipper_application_load_name_and_icon(path, storage, &icon, name)) continue;

            // insertion keeps them sorted by name
            size_t pos = styles->count;
            styles->styles = realloc(styles->styles, (pos + 1) * sizeof(HadalMenuStyle));
            while(pos && furi_string_cmpi(styles->styles[pos - 1].name, name) > 0) {
                styles->styles[pos] = styles->styles[pos - 1];
                pos--;
            }
            styles->styles[pos].file = furi_string_alloc_set_str(file_name);
            styles->styles[pos].name = furi_string_alloc_set(name);
            styles->count++;
        }
    }
    storage_dir_close(dir);

    free(icon);
    furi_string_free(name);
    furi_string_free(path);
    storage_file_free(dir);
}

void hadal_menu_styles_free(HadalMenuStyles* styles) {
    for(size_t i = 0; i < styles->count; i++) {
        furi_string_free(styles->styles[i].file);
        furi_string_free(styles->styles[i].name);
    }
    free(styles->styles);
    styles->styles = NULL;
    styles->count = 0;
}
