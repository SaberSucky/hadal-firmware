#include "boot_profile.h"

#include <furi.h>

typedef struct {
    uint32_t tick;
    const char* name;
} BootProfileMark;

static BootProfileMark boot_profile_marks[BOOT_PROFILE_MAX_MARKS];
static volatile size_t boot_profile_marks_count = 0;

void boot_profile_mark(const char* name) {
    const uint32_t tick = furi_get_tick();
    FURI_CRITICAL_ENTER();
    // Services mark from their own threads; the critical section keeps two marks from taking a slot
    if(boot_profile_marks_count < BOOT_PROFILE_MAX_MARKS) {
        boot_profile_marks[boot_profile_marks_count].tick = tick;
        boot_profile_marks[boot_profile_marks_count].name = name;
        boot_profile_marks_count++;
    }
    FURI_CRITICAL_EXIT();
}

size_t boot_profile_count(void) {
    return boot_profile_marks_count;
}

bool boot_profile_get(size_t index, uint32_t* tick_ms, const char** name) {
    if(index >= boot_profile_marks_count) return false;
    if(tick_ms) *tick_ms = boot_profile_marks[index].tick * 1000u / furi_kernel_get_tick_frequency();
    if(name) *name = boot_profile_marks[index].name;
    return true;
}
