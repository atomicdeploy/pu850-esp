#pragma once

/**
 * @file Public.h
 * @brief Boolean type compatibility macros
 *
 * Provides compatibility macros for boolean types across different
 * build environments and legacy Arduino code that uses 'boolean'.
 */

#include <stdbool.h>

#ifndef bool
#define bool boolean
#endif

#ifndef true
#define true	1
#endif

#ifndef false
#define false	0
#endif
