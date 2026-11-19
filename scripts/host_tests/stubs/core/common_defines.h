/* Host stand-in: the real header pulls in ARM CMSIS. Only the basics are needed here. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef FURI_DEPRECATED
#define FURI_DEPRECATED __attribute__((deprecated))
#endif
