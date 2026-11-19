/* Host stand-in for furi's core defines: only what the infrared library uses. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef COUNT_OF
#define COUNT_OF(x) (sizeof(x) / sizeof(x[0]))
#endif
#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif
