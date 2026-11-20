/* Host driver for applications/services/loader/loader_menu_list.c.
 *
 *   menu_list_host < file          prints one "<kind> <text>" line per entry (kind: A S N P)
 *   menu_list_host fuzz N SEED     N random inputs; checks what must always hold, prints "ok N"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loader_menu_list.h"

static const char kinds[] = {'A', 'S', 'N', 'P'};

static void check(const LoaderMenuList* list) {
    size_t settings = 0;
    if(list->count > LOADER_MENU_LIST_MAX_ENTRIES) abort();
    for(size_t i = 0; i < list->count; i++) {
        const char* text = list->entries[i].text;
        if(!text[0] || strlen(text) > LOADER_MENU_LIST_MAX_LINE) abort();
        if(text[0] == '#' || strchr(text, '\n')) abort();
        if(list->entries[i].kind == LoaderMenuListEntrySettings) settings++;
        for(size_t k = i + 1; k < list->count; k++) {
            if(strcmp(text, list->entries[k].text) == 0) abort(); /* no duplicates */
        }
    }
    if(settings != 1) abort(); /* Settings exactly once, always */
}

static unsigned rng_state;
static unsigned rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

int main(int argc, char** argv) {
    if(argc == 4 && strcmp(argv[1], "fuzz") == 0) {
        const unsigned count = (unsigned)atoi(argv[2]);
        rng_state = (unsigned)atoi(argv[3]) | 1;
        static const char* words[] = {"Apps", "Settings", "NFC", "/ext/apps/a.fap", "#c", " ", "\n", "\r\n", "\t", "x"};
        for(unsigned n = 0; n < count; n++) {
            size_t length = rng() % 3000;
            char* text = malloc(length + 1);
            for(size_t i = 0; i < length;) {
                if(rng() % 3) {
                    const char* w = words[rng() % 10];
                    for(size_t k = 0; w[k] && i < length; k++) text[i++] = w[k];
                } else {
                    text[i++] = (char)(rng() & 0xFF);
                }
            }
            LoaderMenuList list;
            loader_menu_list_parse(length ? text : NULL, length, &list);
            check(&list);
            loader_menu_list_clear(&list);
            if(list.count || list.entries) abort();
            free(text);
        }
        printf("ok %u\n", count);
        return 0;
    }

    static char text[1 << 16];
    const size_t length = fread(text, 1, sizeof(text), stdin);
    LoaderMenuList list;
    loader_menu_list_parse(text, length, &list);
    check(&list);
    for(size_t i = 0; i < list.count; i++) {
        printf("%c %s\n", kinds[list.entries[i].kind], list.entries[i].text);
    }
    loader_menu_list_clear(&list);
    return 0;
}
