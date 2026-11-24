#include "subghz_freqs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER "Filetype: Flipper SubGhz Setting File\nVersion: 1\n"

typedef struct {
    const char* start; /**< the line, its newline excluded */
    size_t length;
} Line;

// The next line; false at the end
static bool next_line(const char** cursor, Line* line) {
    const char* start = *cursor;
    if(!*start) return false;
    const char* end = strchr(start, '\n');
    line->start = start;
    line->length = end ? (size_t)(end - start) : strlen(start);
    *cursor = end ? end + 1 : start + line->length;
    return true;
}

// For an active line "Key: value": whether its key is `key`, and where its value starts
static bool line_key(const Line* line, const char* key, const char** value) {
    const char* p = line->start;
    const char* end = line->start + line->length;
    while(p < end && (*p == ' ' || *p == '\t')) p++;
    const size_t key_length = strlen(key);
    if((size_t)(end - p) <= key_length || strncmp(p, key, key_length) != 0) return false;
    p += key_length;
    while(p < end && (*p == ' ' || *p == '\t')) p++;
    if(p >= end || *p != ':') return false;
    p++;
    while(p < end && (*p == ' ' || *p == '\t')) p++;
    *value = p;
    return true;
}

#define KEY_FREQUENCY "Frequency"
#define KEY_HOPPER    "Hopper_frequency"

// A "<key>: <hz>" line's frequency
static bool line_hz(const Line* line, const char* key, uint32_t* hz) {
    const char* value;
    if(!line_key(line, key, &value)) return false;
    char* end;
    const unsigned long parsed = strtoul(value, &end, 10);
    if(end == value) return false;
    *hz = (uint32_t)parsed;
    return true;
}

static size_t list_key(const char* text, const char* key, uint32_t* out, size_t max) {
    size_t count = 0;
    Line line;
    const char* cursor = text ? text : "";
    while(count < max && next_line(&cursor, &line)) {
        uint32_t hz;
        if(line_hz(&line, key, &hz)) out[count++] = hz;
    }
    return count;
}

size_t subghz_freqs_list(const char* text, uint32_t* out, size_t max) {
    return list_key(text, KEY_FREQUENCY, out, max);
}

size_t subghz_freqs_list_hopper(const char* text, uint32_t* out, size_t max) {
    return list_key(text, KEY_HOPPER, out, max);
}

bool subghz_freqs_standard(const char* text) {
    Line line;
    const char* cursor = text ? text : "";
    while(next_line(&cursor, &line)) {
        const char* value;
        if(line_key(&line, "Add_standard_frequencies", &value)) {
            return strncmp(value, "false", 5) != 0;
        }
    }
    return true;
}

// The text, or a new file's header when there is none
static const char* base(const char* text) {
    return text && text[0] ? text : HEADER;
}

static char* append(char* out, size_t* length, const char* data, size_t size) {
    memcpy(out + *length, data, size);
    *length += size;
    out[*length] = '\0';
    return out;
}

static char* with_key(const char* text, const char* key, uint32_t hz) {
    text = base(text);
    uint32_t existing[256];
    const size_t count = list_key(text, key, existing, 256);
    for(size_t i = 0; i < count; i++) {
        if(existing[i] == hz) return strdup(text);
    }
    char added[48];
    snprintf(added, sizeof(added), "%s: %lu\n", key, (unsigned long)hz);
    const size_t text_length = strlen(text);
    char* out = malloc(text_length + strlen(added) + 2);
    if(!out) return NULL;
    size_t length = 0;
    out[0] = '\0';

    // after the last line with the key, or at the end
    const char* after_last = NULL;
    Line line;
    const char* cursor = text;
    while(next_line(&cursor, &line)) {
        uint32_t f;
        if(line_hz(&line, key, &f)) after_last = cursor;
    }
    const char* split = after_last ? after_last : text + text_length;
    append(out, &length, text, (size_t)(split - text));
    if(length && out[length - 1] != '\n') append(out, &length, "\n", 1);
    append(out, &length, added, strlen(added));
    append(out, &length, split, strlen(split));
    return out;
}

static char* without_key(const char* text, const char* key, uint32_t hz) {
    text = base(text);
    // every line kept ends in a newline, the last one too when the file's did not: one more
    char* out = malloc(strlen(text) + 2);
    if(!out) return NULL;
    size_t length = 0;
    out[0] = '\0';
    Line line;
    const char* cursor = text;
    while(next_line(&cursor, &line)) {
        uint32_t f;
        if(line_hz(&line, key, &f) && f == hz) continue;
        append(out, &length, line.start, line.length);
        append(out, &length, "\n", 1);
    }
    return out;
}

char* subghz_freqs_with(const char* text, uint32_t hz) {
    return with_key(text, KEY_FREQUENCY, hz);
}

char* subghz_freqs_without(const char* text, uint32_t hz) {
    return without_key(text, KEY_FREQUENCY, hz);
}

char* subghz_freqs_with_hopper(const char* text, uint32_t hz) {
    return with_key(text, KEY_HOPPER, hz);
}

char* subghz_freqs_without_hopper(const char* text, uint32_t hz) {
    return without_key(text, KEY_HOPPER, hz);
}

char* subghz_freqs_set_standard(const char* text, bool on) {
    text = base(text);
    const char* setting = on ? "Add_standard_frequencies: true\n" :
                               "Add_standard_frequencies: false\n";
    char* out = malloc(strlen(text) + strlen(setting) + 2);
    if(!out) return NULL;
    size_t length = 0;
    out[0] = '\0';
    bool written = false;
    Line line;
    const char* cursor = text;
    while(next_line(&cursor, &line)) {
        const char* value;
        if(line_key(&line, "Add_standard_frequencies", &value)) {
            if(!written) append(out, &length, setting, strlen(setting));
            written = true;
            continue;
        }
        append(out, &length, line.start, line.length);
        append(out, &length, "\n", 1);
        // a new setting goes right after the header
        if(!written && strncmp(line.start, "Version", 7) == 0) {
            append(out, &length, setting, strlen(setting));
            written = true;
        }
    }
    if(!written) append(out, &length, setting, strlen(setting));
    return out;
}
