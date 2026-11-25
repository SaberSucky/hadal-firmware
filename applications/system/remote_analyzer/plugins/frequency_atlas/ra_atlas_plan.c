#include "ra_atlas_plan.h"

#include <string.h>

/* Six fields: start, end, listen, label, use, limit */
#define RA_ATLAS_BAND_FIELDS 6

static bool ra_atlas_is_space(char c) {
    return (c == ' ') || (c == '\t') || (c == '\r');
}

/** Trim in place: returns the first non-space character, and cuts trailing ones with a NUL. */
static char* ra_atlas_trim(char* s) {
    while(ra_atlas_is_space(*s))
        s++;
    size_t n = strlen(s);
    while(n && ra_atlas_is_space(s[n - 1]))
        s[--n] = '\0';
    return s;
}

/** A whole decimal number of Hz: digits only, no sign, no overflow. Empty is not a number. */
static bool ra_atlas_parse_hz(const char* s, uint32_t* out) {
    if(!*s) return false;
    uint32_t value = 0;
    for(; *s; s++) {
        if((*s < '0') || (*s > '9')) return false;
        const uint32_t digit = (uint32_t)(*s - '0');
        if(value > (UINT32_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    }
    *out = value;
    return true;
}

/** "Band:" value. @return false, leaving the plan as it was, when it does not parse. */
static bool ra_atlas_parse_band(RaAtlasPlan* plan, char* value) {
    if(plan->count >= RA_ATLAS_BANDS_MAX) return false;

    char* field[RA_ATLAS_BAND_FIELDS];
    size_t count = 0;
    char* p = value;
    while(true) {
        field[count++] = p;
        char* bar = strchr(p, '|');
        if(!bar) break;
        // A separator after the last field: one too many
        if(count == RA_ATLAS_BAND_FIELDS) return false;
        *bar = '\0';
        p = bar + 1;
    }
    if(count != RA_ATLAS_BAND_FIELDS) return false;
    for(size_t i = 0; i < RA_ATLAS_BAND_FIELDS; i++)
        field[i] = ra_atlas_trim(field[i]);

    RaAtlasBand band;
    if(!ra_atlas_parse_hz(field[0], &band.start)) return false;
    if(!ra_atlas_parse_hz(field[1], &band.end)) return false;
    if((band.start == 0) || (band.end <= band.start)) return false;

    if(*field[2]) {
        if(!ra_atlas_parse_hz(field[2], &band.listen)) return false;
        if((band.listen < band.start) || (band.listen > band.end)) return false;
    } else {
        band.listen = band.start + (band.end - band.start) / 2;
    }

    if(!*field[3]) return false;
    band.label = field[3];
    band.use = field[4];
    band.limit = field[5];

    plan->bands[plan->count++] = band;
    return true;
}

static void ra_atlas_parse_line(RaAtlasPlan* plan, char* line) {
    line = ra_atlas_trim(line);
    if(!*line || (*line == '#')) return;

    char* colon = strchr(line, ':');
    if(!colon) {
        plan->bad_lines++;
        return;
    }
    *colon = '\0';
    const char* key = ra_atlas_trim(line);
    char* value = ra_atlas_trim(colon + 1);

    if(strcmp(key, "Filetype") == 0) {
        plan->filetype_ok = strcmp(value, RA_ATLAS_FILETYPE) == 0;
    } else if(strcmp(key, "Version") == 0) {
        uint32_t version = 0;
        plan->version_ok = ra_atlas_parse_hz(value, &version) && (version == RA_ATLAS_VERSION);
    } else if(strcmp(key, "Region") == 0) {
        plan->region = value;
    } else if(strcmp(key, "Name") == 0) {
        plan->name = value;
    } else if(strcmp(key, "Source") == 0) {
        plan->source = value;
    } else if(strcmp(key, "Note") == 0) {
        plan->note = value;
    } else if(strcmp(key, "Band") == 0) {
        if(!ra_atlas_parse_band(plan, value)) plan->bad_lines++;
    }
    // Anything else is a key from a later version: skipped, not an error
}

void ra_atlas_plan_parse(RaAtlasPlan* plan, char* text, size_t size) {
    memset(plan, 0, sizeof(*plan));
    plan->region = "";
    plan->name = "";
    plan->source = "";
    plan->note = "";
    if(!text) return;

    // Room for the terminator is the caller's promise; a NUL inside the text ends it there
    text[size] = '\0';
    char* line = text;
    while(line) {
        char* end = strchr(line, '\n');
        if(end) *end = '\0';
        ra_atlas_parse_line(plan, line);
        line = end ? end + 1 : NULL;
    }
}

bool ra_atlas_plan_is_valid(const RaAtlasPlan* plan) {
    return plan->filetype_ok && plan->version_ok && (plan->count > 0);
}

bool ra_atlas_region_code_is_valid(const char* code) {
    if(!code) return false;
    size_t n = 0;
    for(; code[n]; n++) {
        const char c = code[n];
        const bool ok = ((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9')) || (c == '_') ||
                        (c == '-');
        if(!ok || (n >= 15)) return false;
    }
    return n > 0;
}

size_t ra_atlas_format_mhz(uint32_t hz, char* out, size_t out_size) {
    if(!out_size) return 0;
    const uint32_t khz = hz / 1000;
    uint32_t mhz = khz / 1000;
    uint32_t frac = khz % 1000;

    // Digits of the whole part, most significant first
    char digits[11];
    size_t n = 0;
    do {
        digits[n++] = (char)('0' + mhz % 10);
        mhz /= 10;
    } while(mhz);

    size_t at = 0;
    while(n && (at + 1 < out_size))
        out[at++] = digits[--n];

    if(frac) {
        char tail[4] = {
            (char)('0' + frac / 100), (char)('0' + frac / 10 % 10), (char)('0' + frac % 10), 0};
        size_t len = 3;
        while(tail[len - 1] == '0')
            len--;
        if(at + 1 < out_size) out[at++] = '.';
        for(size_t i = 0; (i < len) && (at + 1 < out_size); i++)
            out[at++] = tail[i];
    }
    out[at] = '\0';
    return at;
}

size_t ra_atlas_wrap(const char* text, size_t columns, char* out, size_t out_size) {
    if(!out_size) return 0;
    if(columns == 0) columns = 1;
    size_t at = 0;
    size_t column = 0;

#define RA_ATLAS_PUT(c)                   \
    do {                                  \
        if(at + 1 >= out_size) goto done; \
        out[at++] = (c);                  \
    } while(0)

    const char* p = text ? text : "";
    while(*p) {
        if(*p == '\n') {
            RA_ATLAS_PUT('\n');
            column = 0;
            p++;
            continue;
        }
        if(*p == ' ') {
            // A space at the start of a wrapped line is dropped, so the text stays flush left
            if(column > 0) {
                // Does the next word still fit after this space?
                size_t word = 0;
                while(p[1 + word] && (p[1 + word] != ' ') && (p[1 + word] != '\n'))
                    word++;
                if(column + 1 + word > columns) {
                    RA_ATLAS_PUT('\n');
                    column = 0;
                } else {
                    RA_ATLAS_PUT(' ');
                    column++;
                }
            }
            p++;
            continue;
        }
        if(column >= columns) {
            // A word longer than a line: cut it where the line ends
            RA_ATLAS_PUT('\n');
            column = 0;
        }
        RA_ATLAS_PUT(*p);
        column++;
        p++;
    }

done:
    out[at] = '\0';
    return at;
#undef RA_ATLAS_PUT
}
