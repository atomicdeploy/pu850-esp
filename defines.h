#pragma once

/**
 * @file defines.h
 * @brief Common definitions and configuration includes for the PU850 ESP firmware
 *
 * This file includes shared type definitions, constants, and protocol definitions
 * used across the firmware. It conditionally includes the build-time generated
 * local header when USE_LOCALH is defined.
 */

#include <Arduino.h>

#ifdef USE_LOCALH
#include "~local.h"
#endif

/*
 * Optional, ignored secret definitions. Keep bearer tokens out of command
 * lines, process listings, build logs, and CI metadata.
 */
#if __has_include("~secrets.h")
#include "~secrets.h"
#endif

#include "ProductConfig.h"

/*
 * Keep the historical ShellOnSerial switch while also accepting a numeric
 * build flag that is easier to pass from CI and cross-platform build tools.
 */
#if defined(SHELL_ON_SERIAL) && SHELL_ON_SERIAL && !defined(ShellOnSerial)
#define ShellOnSerial
#endif

// Include shared definitions from the Temp directory
// These files contain type definitions, constants, and protocol specifications
// shared with the main PU unit firmware.
#include "Temp/CommonDefine.h"
#include "Temp/Always.h"
#include "Temp/ESP_Master_Common.h"
#include "Temp/ChartIndex.h"
