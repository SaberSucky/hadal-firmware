#include "infrared_learned.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char library[INFRARED_LEARNED_NAME_SIZE];
    char button[INFRARED_LEARNED_NAME_SIZE];
    uint32_t key;
    uint32_t score;
} Line;

// A name that fits a line: not empty, not too long, no spaces or control characters
static bool name_ok(const char* name) {
    const size_t length = name ? strlen(name) : 0;
    if(length == 0 || length >= INFRARED_LEARNED_NAME_SIZE) return false;
    for(size_t i = 0; i < length; i++) {
        if((unsigned char)name[i] <= ' ') return false;
    }
    return true;
}

// The next line of text into line; false at the end. *ok is false for a line that cannot be read
static bool next_line(const char** text, Line* line, bool* ok) {
    const char* start = *text;
    if(!start || !*start) return false;
    const char* end = strchr(start, '\n');
    const size_t length = end ? (size_t)(end - start) : strlen(start);
    *text = end ? end + 1 : start + length;

    char buffer[2 * INFRARED_LEARNED_NAME_SIZE + 32];
    *ok = false;
    if(length >= sizeof(buffer)) return true;
    memcpy(buffer, start, length);
    buffer[length] = '\0';

    char extra;
    unsigned key, score;
    // the widths are INFRARED_LEARNED_NAME_SIZE - 1
    if(sscanf(buffer, "%31s %31s %8x %u %c", line->library, line->button, &key, &score, &extra) != 4) {
        return true;
    }
    if(!name_ok(line->library) || !name_ok(line->button) || score == 0) return true;
    line->key = key;
    line->score = score > INFRARED_LEARNED_MAX_SCORE ? INFRARED_LEARNED_MAX_SCORE : score;
    *ok = true;
    return true;
}

size_t infrared_learned_find(
    const char* text,
    const char* library,
    const char* button,
    InfraredLearnedCode* codes,
    size_t max) {
    size_t count = 0;
    Line line;
    bool ok;
    while(next_line(&text, &line, &ok)) {
        if(!ok || strcmp(line.library, library) != 0 || strcmp(line.button, button) != 0) continue;
        // best first; of equal scores, the one listed first
        size_t at = count;
        while(at > 0 && codes[at - 1].score < line.score) {
            at--;
        }
        if(at >= max) continue;
        if(count < max) count++;
        memmove(&codes[at + 1], &codes[at], (count - 1 - at) * sizeof(InfraredLearnedCode));
        codes[at] = (InfraredLearnedCode){.key = line.key, .score = line.score};
    }
    return count;
}

char* infrared_learned_bump(
    const char* text,
    const char* library,
    const char* button,
    uint32_t key,
    InfraredLearnedWeight weight) {
    if(!name_ok(library) || !name_ok(button)) return NULL;

    // as many lines as the text has (and the one added), not always the most it may hold: 201
    // lines took 14 KB on every "it worked" and resend
    size_t capacity = 2;
    for(const char* p = text; p && *p; p++) {
        if(*p == '\n') capacity++;
    }
    if(capacity > INFRARED_LEARNED_MAX_LINES + 1) capacity = INFRARED_LEARNED_MAX_LINES + 1;
    Line* lines = malloc(sizeof(Line) * capacity);
    if(!lines) return NULL;
    size_t count = 0;
    bool found = false;
    Line line;
    bool ok;
    while(next_line(&text, &line, &ok)) {
        if(!ok) continue;
        if(strcmp(line.library, library) == 0 && strcmp(line.button, button) == 0 && line.key == key) {
            if(found) continue; // a second copy of the line
            found = true;
            line.score += (uint32_t)weight;
            if(line.score > INFRARED_LEARNED_MAX_SCORE) line.score = INFRARED_LEARNED_MAX_SCORE;
        }
        if(count <= INFRARED_LEARNED_MAX_LINES) lines[count++] = line;
        if(count > INFRARED_LEARNED_MAX_LINES) {
            // full: the lowest score goes (of equal ones, the oldest), never the one bumped
            size_t drop = count;
            for(size_t i = 0; i < count; i++) {
                const bool bumped = strcmp(lines[i].library, library) == 0 &&
                                    strcmp(lines[i].button, button) == 0 && lines[i].key == key;
                if(bumped) continue;
                if(drop == count || lines[i].score < lines[drop].score) drop = i;
            }
            memmove(&lines[drop], &lines[drop + 1], (count - 1 - drop) * sizeof(Line));
            count--;
        }
    }
    if(!found) {
        Line added = {.key = key, .score = (uint32_t)weight};
        snprintf(added.library, sizeof(added.library), "%s", library);
        snprintf(added.button, sizeof(added.button), "%s", button);
        if(count == INFRARED_LEARNED_MAX_LINES) {
            size_t drop = 0;
            for(size_t i = 1; i < count; i++) {
                if(lines[i].score < lines[drop].score) drop = i;
            }
            memmove(&lines[drop], &lines[drop + 1], (count - 1 - drop) * sizeof(Line));
            count--;
        }
        lines[count++] = added;
    }

    const size_t line_max = 2 * INFRARED_LEARNED_NAME_SIZE + 24;
    // the text's exact size, measured first
    size_t size = 1;
    for(size_t i = 0; i < count; i++) {
        size += (size_t)snprintf(
            NULL,
            0,
            "%s %s %08lX %lu\n",
            lines[i].library,
            lines[i].button,
            (unsigned long)lines[i].key,
            (unsigned long)lines[i].score);
    }
    char* out = malloc(size);
    if(out) {
        size_t length = 0;
        out[0] = '\0';
        for(size_t i = 0; i < count; i++) {
            length += (size_t)snprintf(
                out + length,
                line_max,
                "%s %s %08lX %lu\n",
                lines[i].library,
                lines[i].button,
                (unsigned long)lines[i].key,
                (unsigned long)lines[i].score);
        }
    }
    free(lines);
    return out;
}
