/* Host stand-in for furi's checks: a failed check aborts, so the sanitizers report it. */
#pragma once
#include <stdio.h>
#include <stdlib.h>

#define furi_crash(...) \
    do { fprintf(stderr, "furi_crash at %s:%d\n", __FILE__, __LINE__); abort(); } while(0)
#define furi_check(x, ...) \
    do { if(!(x)) { fprintf(stderr, "furi_check failed: %s at %s:%d\n", #x, __FILE__, __LINE__); abort(); } } while(0)
#define furi_assert(x, ...) furi_check(x)
