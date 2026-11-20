/* Host driver for applications/services/desktop/helpers/desktop_keybinds.c.
 *
 * stdin: lines of a keybinds file; for each, prints "<bind name>|<action>|<desktop action>|<app>"
 * (the app a file action opens in, or -), or "-" for a line that is not a binding.
 * argv: fuzz N SEED   random lines, under the sanitizers
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "desktop_keybinds.h"

static unsigned rng_state;
static unsigned rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

int main(int argc, char** argv) {
    if(argc == 4 && strcmp(argv[1], "fuzz") == 0) {
        rng_state = (unsigned)atoi(argv[3]) | 1;
        static const char* const parts[] = {"up", "press", "hold", ":", " ", "ok", "\t", "x", "/ext/a.sub", "LOCK"};
        for(int n = 0; n < atoi(argv[2]); n++) {
            char line[96];
            size_t length = 0;
            const int pieces = (int)(rng() % 8);
            for(int p = 0; p < pieces; p++) {
                const char* part = parts[rng() % 10];
                const size_t l = strlen(part);
                if(length + l >= sizeof(line)) break;
                memcpy(line + length, part, l);
                length += l;
            }
            if(rng() % 4 == 0 && length) line[rng() % length] = (char)rng();
            // no terminator: the parser must keep to length
            char* exact = malloc(length ? length : 1);
            memcpy(exact, line, length);
            DesktopKeybind bind;
            const char* action;
            size_t action_length;
            if(desktop_keybinds_parse_line(exact, length, &bind, &action, &action_length)) {
                if(bind >= DesktopKeybindCount || action < exact || action + action_length > exact + length) abort();
            }
            free(exact);
        }
        printf("ok\n");
        return 0;
    }
    char line[512];
    while(fgets(line, sizeof(line), stdin)) {
        DesktopKeybind bind;
        const char* action;
        size_t action_length;
        if(!desktop_keybinds_parse_line(line, strlen(line), &bind, &action, &action_length)) {
            printf("-\n");
            continue;
        }
        char text[512];
        memcpy(text, action, action_length);
        text[action_length] = '\0';
        const char* app = desktop_keybind_app_for_file(text);
        printf("%s|%s|%d|%s\n", desktop_keybind_name(bind), text, desktop_keybind_action(text), app ? app : "-");
    }
    return 0;
}
