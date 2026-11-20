#include "loader_menu_list.h"

#include <stdlib.h>
#include <string.h>

static bool menu_list_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

static bool menu_list_contains(const LoaderMenuList* list, const char* text) {
    for(size_t i = 0; i < list->count; i++) {
        if(strcmp(list->entries[i].text, text) == 0) return true;
    }
    return false;
}

static void menu_list_add(
    LoaderMenuList* list,
    LoaderMenuListEntryKind kind,
    const char* text,
    size_t length) {
    char* copy = malloc(length + 1);
    if(!copy) return;
    memcpy(copy, text, length);
    copy[length] = '\0';
    if(menu_list_contains(list, copy)) {
        free(copy);
        return;
    }
    LoaderMenuListEntry* grown =
        realloc(list->entries, (list->count + 1) * sizeof(LoaderMenuListEntry));
    if(!grown) {
        free(copy);
        return;
    }
    list->entries = grown;
    list->entries[list->count].kind = kind;
    list->entries[list->count].text = copy;
    list->count++;
}

static LoaderMenuListEntryKind menu_list_kind(const char* text, size_t length) {
    if(length == strlen(LOADER_MENU_LIST_APPS_NAME) &&
       memcmp(text, LOADER_MENU_LIST_APPS_NAME, length) == 0) {
        return LoaderMenuListEntryApps;
    }
    if(length == strlen(LOADER_MENU_LIST_SETTINGS_NAME) &&
       memcmp(text, LOADER_MENU_LIST_SETTINGS_NAME, length) == 0) {
        return LoaderMenuListEntrySettings;
    }
    return text[0] == '/' ? LoaderMenuListEntryPath : LoaderMenuListEntryName;
}

void loader_menu_list_parse(const char* text, size_t length, LoaderMenuList* list) {
    list->entries = NULL;
    list->count = 0;
    bool has_settings = false;

    size_t start = 0;
    while(text && start < length) {
        size_t end = start;
        while(end < length && text[end] != '\n')
            end++;
        const size_t next = end + 1;

        // trim
        while(start < end && menu_list_is_space(text[start]))
            start++;
        while(end > start && menu_list_is_space(text[end - 1]))
            end--;
        const size_t line_length = end - start;

        if(line_length && text[start] != '#' && line_length <= LOADER_MENU_LIST_MAX_LINE &&
           memchr(text + start, '\0', line_length) == NULL) {
            const LoaderMenuListEntryKind kind = menu_list_kind(text + start, line_length);
            // Settings always gets a place, even in a list that is full
            if(kind == LoaderMenuListEntrySettings ||
               list->count < LOADER_MENU_LIST_MAX_ENTRIES - 1) {
                if(kind == LoaderMenuListEntrySettings) has_settings = true;
                menu_list_add(list, kind, text + start, line_length);
            }
        }
        start = next;
    }

    if(!has_settings) {
        menu_list_add(
            list,
            LoaderMenuListEntrySettings,
            LOADER_MENU_LIST_SETTINGS_NAME,
            strlen(LOADER_MENU_LIST_SETTINGS_NAME));
    }
}

void loader_menu_list_clear(LoaderMenuList* list) {
    for(size_t i = 0; i < list->count; i++) {
        free(list->entries[i].text);
    }
    free(list->entries);
    list->entries = NULL;
    list->count = 0;
}
