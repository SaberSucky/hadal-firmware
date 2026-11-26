#include "ra_atlas_save.h"
#include "ra_atlas_plan.h"

#include <stdio.h>
#include <string.h>

/** The value of line "<key>: <value>" starting at @p line, or NULL when it is another key */
static const char* ra_atlas_save_value(const char* line, const char* key) {
    const size_t length = strlen(key);
    if(strncmp(line, key, length) != 0 || line[length] != ':') return NULL;
    const char* value = line + length + 1;
    while(*value == ' ')
        value++;
    return value;
}

/** Where the line after @p line starts, or the NUL ending the text */
static const char* ra_atlas_save_next(const char* line) {
    const char* end = strchr(line, '\n');
    return end ? end + 1 : line + strlen(line);
}

/** Whether a value has something before its line ends */
static bool ra_atlas_save_filled(const char* value) {
    return value && *value && *value != '\r' && *value != '\n';
}

static uint32_t ra_atlas_save_hz(const char* value) {
    uint32_t hz = 0;
    for(; *value >= '0' && *value <= '9'; value++) {
        const uint32_t digit = (uint32_t)(*value - '0');
        if(hz > (UINT32_MAX - digit) / 10) return 0;
        hz = hz * 10 + digit;
    }
    return hz;
}

uint32_t ra_atlas_capture_frequency(const char* text) {
    if(!text) return 0;
    for(const char* line = text; *line; line = ra_atlas_save_next(line)) {
        const char* value = ra_atlas_save_value(line, "Frequency");
        if(value) return ra_atlas_save_hz(value);
    }
    return 0;
}

bool ra_atlas_capture_is_key_file(const char* text) {
    if(!text || !*text) return false;
    const char* filetype = ra_atlas_save_value(text, "Filetype");
    const size_t length = strlen(RA_ATLAS_SAVE_FILETYPE);
    if(!filetype || strncmp(filetype, RA_ATLAS_SAVE_FILETYPE, length) != 0) return false;
    const char after = filetype[length];
    if(after != '\0' && after != '\r' && after != '\n') return false;

    bool version = false;
    bool protocol = false;
    for(const char* line = ra_atlas_save_next(text); *line; line = ra_atlas_save_next(line)) {
        if(ra_atlas_save_filled(ra_atlas_save_value(line, "Version"))) version = true;
        if(ra_atlas_save_filled(ra_atlas_save_value(line, "Protocol"))) protocol = true;
    }
    return version && protocol && ra_atlas_capture_frequency(text) > 0;
}

/** Letters, digits and . + - as they are; the rest would trip a file name up somewhere */
static char ra_atlas_save_char(char c) {
    if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
    if(c == '.' || c == '+' || c == '-') return c;
    return '_';
}

size_t ra_atlas_save_name(
    const char* protocol,
    uint32_t hz,
    unsigned copy,
    char* out,
    size_t out_size) {
    if(!out_size) return 0;
    if(!protocol || !*protocol) protocol = "Signal";

    char mhz[16];
    ra_atlas_format_mhz(hz, mhz, sizeof(mhz));
    char tail[32];
    if(copy > 1) {
        snprintf(tail, sizeof(tail), "_%s_%u", mhz, copy);
    } else {
        snprintf(tail, sizeof(tail), "_%s", mhz);
    }

    // the protocol cut first, so the frequency and the copy number always make it in
    const size_t tail_length = strlen(tail);
    size_t room = out_size - 1;
    room = room > tail_length ? room - tail_length : 0;
    size_t n = 0;
    for(; protocol[n] && n < room; n++) {
        out[n] = ra_atlas_save_char(protocol[n]);
    }
    for(size_t i = 0; tail[i] && n + 1 < out_size; i++) {
        out[n++] = tail[i];
    }
    out[n] = '\0';
    return n;
}
