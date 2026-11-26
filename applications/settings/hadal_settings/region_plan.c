#include "region_plan.h"

#include <furi.h>
#include <furi_hal_subghz.h>

// the atlas's own parser, so that both read a band plan the same way
#include "../../system/remote_analyzer/plugins/frequency_atlas/ra_atlas_plan.c"

#define REGION_DIR EXT_PATH("frequency_atlas")

_Static_assert(HADAL_REGION_BANDS_MAX == RA_ATLAS_BANDS_MAX, "The atlas reads another band count");

bool hadal_region_plan_load(Storage* storage, const char* code, HadalRegionPlan* plan) {
    memset(plan, 0, sizeof(*plan));
    if(!ra_atlas_region_code_is_valid(code) || strcmp(code, "off") == 0) return false;

    FuriString* path = furi_string_alloc_printf("%s/%s.txt", REGION_DIR, code);
    File* file = storage_file_alloc(storage);
    char* text = NULL;
    RaAtlasPlan* parsed = NULL;
    bool ok = false;
    do {
        if(!storage_file_open(file, furi_string_get_cstr(path), FSAM_READ, FSOM_OPEN_EXISTING)) {
            break;
        }
        const uint64_t size = storage_file_size(file);
        if(size == 0 || size > RA_ATLAS_FILE_MAX) break;
        // one byte past the file for the parser's last terminator
        text = malloc((size_t)size + 1);
        if(storage_file_read(file, text, (size_t)size) != size) break;
        parsed = malloc(sizeof(RaAtlasPlan));
        ra_atlas_plan_parse(parsed, text, (size_t)size);
        if(!ra_atlas_plan_is_valid(parsed)) break;

        for(size_t i = 0; i < parsed->count; i++) {
            const uint32_t hz = parsed->bands[i].listen;
            if(!furi_hal_subghz_is_frequency_valid(hz)) {
                plan->out_of_range++;
                continue;
            }
            bool repeat = false;
            for(size_t j = 0; j < plan->count; j++) {
                if(plan->hz[j] == hz) repeat = true;
            }
            if(!repeat) plan->hz[plan->count++] = hz;
        }
        // the plan's own short name, as the atlas's header shows it, else the code in capitals
        const char* region = parsed->region;
        if(*region) {
            strlcpy(plan->name, region, sizeof(plan->name));
        } else {
            size_t i = 0;
            for(; code[i] && i + 1 < sizeof(plan->name); i++) {
                const char c = code[i];
                plan->name[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
            }
            plan->name[i] = '\0';
        }
        ok = true;
    } while(false);

    free(parsed);
    free(text);
    storage_file_close(file);
    storage_file_free(file);
    furi_string_free(path);
    return ok;
}
