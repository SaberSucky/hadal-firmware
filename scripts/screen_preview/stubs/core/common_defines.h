#pragma once
/* furi/core/common_defines.h without the Cortex-M parts */
#include <stdbool.h>
#include <stdint.h>
#include <core/core_defines.h>

#define FURI_NORETURN      __attribute__((noreturn))
#define FURI_WARN_UNUSED   __attribute__((warn_unused_result))
#define FURI_DEPRECATED    __attribute__((deprecated))
#define FURI_WEAK          __attribute__((weak))
#define FURI_PACKED        __attribute__((packed))
#define FURI_ALWAYS_INLINE __attribute__((always_inline)) inline
#define FURI_CHECK_RETURN  __attribute__((__warn_unused_result__))
/* newlib's, which furi's headers use for printf-style checks */
#ifndef _ATTRIBUTE
#define _ATTRIBUTE(attrs) __attribute__(attrs)
#endif
