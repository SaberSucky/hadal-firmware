#include "infrared_raw_trim.h"

// Raw codes are trimmed and spaced here for universal remotes, which send many codes in a row.
// No firmware dependencies, so that the host tests build it as it is.

// Turbo mode, raw codes: five times the longest mark sent, within these bounds
#define INFRARED_TURBO_RAW_GAP_MIN_US      (20000U)
#define INFRARED_TURBO_RAW_GAP_MAX_US      (50000U)
#define INFRARED_TURBO_RAW_GAP_PER_MARK_US (5U)

// A space longer than this separates frames
#define INFRARED_RAW_FRAME_GAP_US      (10000U)
// Fast mode: keep at most this many identical frames in a row (some TVs need a repeat)
#define INFRARED_RAW_MAX_SAME_FRAMES   (2U)
// Turbo mode: one frame
#define INFRARED_RAW_TURBO_SAME_FRAMES (1U)
// Both modes: three for Sony-like codes, which their devices only accept repeated (the parsed
// SIRC encoder sends three as well)
#define INFRARED_RAW_SONY_FRAMES       (3U)
// Timings within this tolerance (or a quarter of the timing) are considered identical
#define INFRARED_RAW_SAME_TOLERANCE_US (150U)

static bool infrared_raw_timing_same(uint32_t a, uint32_t b) {
    const uint32_t diff = a > b ? a - b : b - a;
    const uint32_t quarter = a / 4;
    const uint32_t tolerance =
        quarter > INFRARED_RAW_SAME_TOLERANCE_US ? quarter : INFRARED_RAW_SAME_TOLERANCE_US;
    return diff <= tolerance;
}

static bool infrared_raw_frames_same(
    const uint32_t* timings,
    size_t a_start,
    size_t a_len,
    size_t b_start,
    size_t b_len) {
    if(a_len != b_len) return false;
    for(size_t i = 0; i < a_len; i++) {
        if(!infrared_raw_timing_same(timings[a_start + i], timings[b_start + i])) return false;
    }
    return true;
}

static bool infrared_raw_within(uint32_t value, uint32_t target, uint32_t tolerance) {
    return value + tolerance >= target && value <= target + tolerance;
}

// A Sony (SIRC) frame starts with a 2.4 ms mark and a 0.6 ms space
static bool infrared_raw_frame_is_sony(const uint32_t* frame, size_t len) {
    return len >= 2 && infrared_raw_within(frame[0], 2400, 600) &&
           infrared_raw_within(frame[1], 600, 200);
}

// An NEC repeat frame (button held) is a 9 ms mark, a 2.25 ms space and a short mark
static bool infrared_raw_frame_is_nec_repeat(const uint32_t* frame, size_t len) {
    return len >= 2 && len <= 3 && infrared_raw_within(frame[0], 9000, 1800) &&
           infrared_raw_within(frame[1], 2250, 600);
}

typedef struct {
    bool turbo;
    bool first;
    uint32_t max_same;
    uint32_t same_count;
    size_t prev_start;
    size_t prev_len;
} InfraredRawTrim;

// Whether the frame [start, start + len) and everything after it are dropped
static bool infrared_raw_trim_frame(
    InfraredRawTrim* trim,
    const uint32_t* timings,
    size_t start,
    size_t len) {
    if(trim->first) {
        trim->first = false;
        trim->same_count = 1;
        if(infrared_raw_frame_is_sony(timings + start, len)) {
            trim->max_same = INFRARED_RAW_SONY_FRAMES;
        } else {
            trim->max_same = trim->turbo ? INFRARED_RAW_TURBO_SAME_FRAMES :
                                           INFRARED_RAW_MAX_SAME_FRAMES;
        }
    } else {
        if(trim->turbo && infrared_raw_frame_is_nec_repeat(timings + start, len)) return true;
        if(infrared_raw_frames_same(timings, trim->prev_start, trim->prev_len, start, len)) {
            trim->same_count++;
        } else {
            trim->same_count = 1;
        }
        if(trim->same_count > trim->max_same) return true;
    }
    trim->prev_start = start;
    trim->prev_len = len;
    return false;
}

size_t infrared_raw_trim_length(const uint32_t* timings, size_t size, bool turbo) {
    if(!timings || size == 0) return 0;

    InfraredRawTrim trim = {
        .turbo = turbo,
        .first = true,
    };
    size_t frame_start = 0;

    // Timings start with a mark: odd indices are spaces
    for(size_t i = 1; i < size; i += 2) {
        if(timings[i] <= INFRARED_RAW_FRAME_GAP_US) continue;
        // Frame [frame_start, i) ends here, followed by a long space at i
        if(infrared_raw_trim_frame(&trim, timings, frame_start, i - frame_start)) {
            // Drop this frame and everything after it (the space before it too). Never the
            // first frame, so frame_start >= 2 here and the result ends on a mark.
            return frame_start - 1;
        }
        frame_start = i + 1;
    }

    // Last frame (no trailing long space)
    if(frame_start < size &&
       infrared_raw_trim_frame(&trim, timings, frame_start, size - frame_start)) {
        return frame_start - 1;
    }
    return size;
}

uint32_t infrared_raw_trim_turbo_gap_us(const uint32_t* timings, size_t length) {
    uint32_t longest_mark = 0;
    for(size_t i = 0; timings && i < length; i += 2) {
        if(timings[i] > longest_mark) longest_mark = timings[i];
    }
    // Saturate instead of overflowing on a nonsense capture
    uint32_t gap = longest_mark > INFRARED_TURBO_RAW_GAP_MAX_US ?
                       INFRARED_TURBO_RAW_GAP_MAX_US :
                       longest_mark * INFRARED_TURBO_RAW_GAP_PER_MARK_US;
    if(gap < INFRARED_TURBO_RAW_GAP_MIN_US) gap = INFRARED_TURBO_RAW_GAP_MIN_US;
    if(gap > INFRARED_TURBO_RAW_GAP_MAX_US) gap = INFRARED_TURBO_RAW_GAP_MAX_US;
    return gap;
}
